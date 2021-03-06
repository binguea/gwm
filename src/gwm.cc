 /* Copyright (C) 1999 - 2002 Anders Holst
  *
  * This program is free software; you can redistribute it and/or
  * modify it under the terms of the GNU General Public License as
  * published by the Free Software Foundation; either version 2, or
  * (at your option) any later version.
  * 
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  * 
  * You should have received a copy of the GNU General Public License
  * along with this software; see the file COPYING.  If not, write to
  * the Free Software Foundation, Inc., 59 Temple Place, Suite 330,
  * Boston, MA 02111-1307 USA
  */


/*********************************\
* 			          *
* GENERIC WINDOW MANAGER for X11  *
* 			          *
* 	main body	          *
* 			          *
\*********************************/

/* include */

#include <guile/gh.h>
#include <stdio.h>

#include "gwm.hh"
#include "gwmfunc.hh"
#include "error.hh"
#include "paint.hh"
#include "deco.hh"
#include "event.hh"
#include "fsm.hh"
#include "screen.hh"
#include "client.hh"
#include "drawing.hh"

extern "C" {
#ifdef HAVE_GUILEREADLINE
#ifdef HAVE_GUILE_READLINE_READLINE_H
#include <guile-readline/readline.h>
#else
extern void scm_readline_init_ports (SCM inp, SCM outp);
#endif
#endif
#include <readline/readline.h>
#include <readline/history.h>
#ifdef MISSING_RL_DEPREP_TERMINAL_DECL
void rl_deprep_terminal(void);
#endif
#ifdef NEED_RL_FUNCTION_CAST
#define RL_FUNCTION_CAST (VFunction *) 
#else
#define RL_FUNCTION_CAST 
#endif
}
#include <X11/Xresource.h>

ScreenContext** GWMManagedScreens;
ScreenContext* Context;
ScreenContext* cmd_screen;

Display    *dpy;		/* THE display */
Time 	GWMTime = 0;/* the latest known server date */
XContext 	deco_context;	/* to retrieve wobs via hooks */
XContext 	client_context;	/* to retrieve wobs via clients */
int		GWM_ShapeExtension = 0; /* display has shape extension ? */
int		GWM_ShapeEventBase = 0; /* first event # of shape ext. */
int		GWM_ShapeErrorBase = 0; /* first error # of shape ext. */

/* switches */
int GWM_Mapall = 0;
int GWM_FreezeServer = 1;
int GWM_Quiet = 0;
int GWM_Synchronize = 0;
char* GWM_DisplayName = 0;
int GWM_ScreenCount = 0;       
int GWM_Monoscreen = 0;
int GWM_No_set_focus = 0;
int GWM_WidgetMode = 0;
int GWM_RetryStart = 0;
char* GWM_ScreensNotManaged = 0;
int GWM_kill_pid = 0;
int GWM_kill_pid_sig = 0;
char* GWM_path = 0;
SCM GWM_host_name = 0;
char *GWM_user_profile_name;

/* static data for remebering GWM state */
int GWM_argc;			/* unix argc argv of GWM */
char ** GWM_argv;
char ** GWM_restart_argv = 0;
unsigned int GWM_new_window_id = 0;
int GWM_is_starting = 0;
int GWM_is_ending = 0;
int GWM_time_of_last_bus_error = 0;
int GWM_processing_frozen = 0;
int GWM_rubber_feedback = 0;

FILE* gwmlog = 0;   // log file used for debugging

#define GWMLOG(str, obj) if(gwmlog) { fprintf(gwmlog, str, obj); fflush(gwmlog); }

/* masks */
unsigned int ClientMask         = SubstructureRedirectMask | FocusChangeMask | LeaveWindowMask | EnterWindowMask;
unsigned int ClientClientMask   = PropertyChangeMask
    				| StructureNotifyMask | ColormapChangeMask;
unsigned int RootMask	        = SubstructureRedirectMask
    				| StructureNotifyMask | ColormapChangeMask
    				| PropertyChangeMask | FocusChangeMask | LeaveWindowMask | EnterWindowMask;
unsigned int RootMask2	        = FocusChangeMask | LeaveWindowMask | EnterWindowMask;
unsigned int MenuMask 	        = StructureNotifyMask;
unsigned int WobMask 	        = FocusChangeMask | LeaveWindowMask | EnterWindowMask;

/* X Atoms */
Atom	XA_WM_STATE;
Atom	XA_WM_COLORMAP_WINDOWS;
Atom	XA_WM_CHANGE_STATE;
Atom	XA_WM_PROTOCOLS;
Atom	XA_WM_TAKE_FOCUS;
Atom	XA_WM_SAVE_YOURSELF;
Atom	XA_WM_DELETE_WINDOW;
Atom	XA_GNOME_SUPPORTING;
Atom	XA_GNOME_PROTOCOLS;
Atom    XA_GWM_RUNNING;

SCM DefaultClientClass;
SCM DefaultClientName;
SCM DefaultWindowName;
SCM DefaultMachineName;
SCM DefaultIconName;
SCM DefaultFont;


/* local */

/* routines */

int gwm_init_profile();
void RegisterScreens();
int GWM_Initialize();
void GWM_SignalsInit();
void GWM_getopts(int argc, char** argv);
void handle_stdin_command(char* cmd);
char* gwmprompt(int n);

extern void SetUpDefaults();
extern void GWM_init_guile();

int GWM_stdin = 1;
int GWM_prompt = 1;

/*
 * Main body of GWM:
 * 	- parse arguments
 * 	- initialize X
 * 	- initialize Guile
 * 	- read user profile
 * 	- decorate all already present windows
 * 	- enter main loop: GWM_ProcessEvents
 */

void main2(int argc, char** argv)
{
  ScreenContext* scr;

  GWM_Initialize();

  GWM_is_starting = 1;
  if (gwm_init_profile())
    fprintf(stderr, "GWM Warning: error while reading profile\n");
  FOR_ALL_SCREENS(scr) {  // decorate and open all screens and their windows
    scr->Initialize();
  }
  GWM_is_starting = 0;

  /* send signal if a process is waiting for gwm to come up */
  if (GWM_kill_pid) {
    kill(GWM_kill_pid, GWM_kill_pid_sig);
  }

  if (GWM_stdin && GWM_prompt &&
      !isatty(SCM_FSTREAM(scm_current_input_port())->fdes))
    GWM_stdin = 0;
  if (GWM_stdin) {
#ifdef HAVE_GUILEREADLINE
    scm_readline_init_ports(scm_current_input_port(), scm_current_output_port());
#endif
    rl_callback_handler_install(gwmprompt(0), RL_FUNCTION_CAST handle_stdin_command);
  }
  while (1) 	/* MAIN LOOP */
    gwm_call_with_catch((scm_t_catch_body) GWM_ProcessEvents, (void*)1);
}

int main(int argc, char** argv)
{
  GWM_argc = argc;
  GWM_argv = argv;
  GWM_getopts(argc, argv);		/* options & open display */
  gh_enter(argc, argv, main2);
  return 0;
}

static int global_handle_stdin_level = 0;
static int global_handle_stdin_shown = 1;
static char* global_handle_stdin_cmd = 0;
static char* global_handle_stdin_saved = 0;

char* gwmprompt(int n)
{
  static char buf[20];
  if (!GWM_prompt)
    return "";
  if (n == 0)
    sprintf(buf, "gwm> ");
  else
    sprintf(buf, "gwm:%d> ", n);
  return buf;
}

void handle_stdin_check_prompt()
{
  if (!global_handle_stdin_shown) {
    rl_callback_handler_install(gwmprompt(global_handle_stdin_level),
                                RL_FUNCTION_CAST handle_stdin_command);
    global_handle_stdin_shown = 1;
  }
}

void handle_stdin_command_inner(char* cmd)
{
  SCM res;
  ScreenContext* currscr = Context;
  add_history(cmd);
  SetContext(cmd_screen);
  res = scm_c_eval_string(cmd);
  free(cmd);
  cmd_screen = Context;
  SetContext(currscr);
  if (global_handle_stdin_shown) {
    scm_newline(scm_current_output_port());
    global_handle_stdin_saved = strcpy(new char[strlen(rl_line_buffer)+1],
                                       rl_line_buffer);
  }
  if (res != SCM_UNSPECIFIED) {
    scm_write(res, scm_current_output_port());
    scm_newline(scm_current_output_port());
  }
}

void handle_stdin_command(char* cmd)
{
  if (cmd && *cmd) {
    global_handle_stdin_cmd = cmd;
    rl_callback_handler_install("", RL_FUNCTION_CAST handle_stdin_command);
    global_handle_stdin_shown = 0;
  } else if (cmd)
    free(cmd);
}

void handle_stdin_char()
{
  SCM cmd;
  global_handle_stdin_cmd = 0;
  handle_stdin_check_prompt();
  rl_callback_read_char();
  if (global_handle_stdin_cmd) {
    global_handle_stdin_level++;
    gwm_call_with_catch((scm_t_catch_body) handle_stdin_command_inner,
                        global_handle_stdin_cmd);
    global_handle_stdin_level--;
    rl_callback_handler_install(gwmprompt(global_handle_stdin_level),
                                RL_FUNCTION_CAST handle_stdin_command);
    if (global_handle_stdin_shown && global_handle_stdin_saved) {
      rl_insert_text(global_handle_stdin_saved);
      rl_redisplay();
      delete [] global_handle_stdin_saved;
      global_handle_stdin_saved = 0;
    }
    global_handle_stdin_shown = 1;
  }
}

int gwm_proper_time()
{
  int time;

#ifdef HAVE_FTIME
  struct timeb time_bsd;
  ftime(&time_bsd);
  time = time_bsd.time * 1000 + time_bsd.millitm;
#else
  struct timeval tv;
  struct timezone tz;
  gettimeofday(&tv, &tz);
  time = tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif

  return time;
}

int GWM_AwaitInput(int maxtime)
{
  struct timeval to;
  fd_set fds;
  int s;
  FD_ZERO(&fds);
  if (GWM_stdin)
    FD_SET(SCM_FSTREAM(scm_current_input_port())->fdes, &fds);
  FD_SET(ConnectionNumber(dpy), &fds);
  if (maxtime >= 0) {
    to.tv_sec = maxtime/1000;
    to.tv_usec = (maxtime%1000)*1000;
  }
  /* Threads are not used in GWM yet (scm_internal_select) */
  s = select(FD_SETSIZE, &fds, 0, 0, (maxtime<0 ? 0 : &to));
  if (s <= 0)
    return 0;
  else if (FD_ISSET(SCM_FSTREAM(scm_current_input_port())->fdes, &fds))
    return 1;
  else
    return 2;
}

void GWM_InternSleep(int rest)
{
  int end, s;
  end = gwm_proper_time() + rest;
  GWM_ProcessEvents(0);
  rest = end - gwm_proper_time();
  while (rest > 0) {
    s = GWM_AwaitInput(rest);
    if (s > 0)
      GWM_ProcessEvents(0);
    rest = end - gwm_proper_time();
  }
}

/*
 * search all wobs for the one having the same X window as the event and
 * return it.
 */

Decoration* LookUpDeco(Window window)
{
  Decoration* wob;
  if (!XFindContext(dpy, window, deco_context, (char **)&wob))
    return wob;
  else
    return 0;
}

IClient* LookUpInnerClient(Window window)
{
  IClient* cl;
  if (!XFindContext(dpy, window, client_context, (char **)&cl))
    return cl;
  else
    return 0;
}

ClientWindow* LookUpClient(Window window)
{
  IClient* cl;
  if (!XFindContext(dpy, window, client_context, (char **)&cl))
    return cl->Win();
  else
    return 0;
}

/* find the root of a window */

ScreenContext* ContextOfXWindow(Window window)
{
  Window root;
  unsigned int measures[6];
  int res;
  TrapXErrors(res = XGetGeometry(dpy, window, &root,
                                 (int *)&measures[0], (int *)&measures[1],
                                 &measures[2], &measures[3],
                                 &measures[4], &measures[5]));
  if (!res) 
    return 0;
  res = ScreenOfRoot(root);
  if (res == -1)
    return 0;
  return GWMManagedScreens[res];
}

/* updating a colormap for a watched window in a WM_COLORMAP_WINDOWS list
 */
void Update_colormap_in_colormap_windows_list(XColormapEvent* evt)
{
  ClientWindow* cw;
  ScreenContext* scr;
  if (!(scr = ContextOfXWindow(evt->window)))
    return;				/* if no more window, returns */
  cw = scr->ColormapWindow();
  if (cw && evt->window == cw->ColormapIndexWindow() && evt->c_new)
    scr->AnnounceColormap(cw, evt->colormap);
}


char* debug_event_name(XEvent* ev)
{
  switch (ev->type) {
  case KeyPress: return "KeyPress";
  case KeyRelease: return "KeyRelease";
  case ButtonPress: return "ButtonPress";
  case ButtonRelease: return "ButtonRelease";
  case MotionNotify: return "PointerMotion";
  case EnterNotify: return "EnterWindow";
  case LeaveNotify: return "LeaveWindow";
  case FocusIn: return "FocusIn";
  case FocusOut: return "FocusOut";
  case KeymapNotify: return "KeymapNotify";
  case Expose:
  case GraphicsExpose:
  case NoExpose: return "Exposure";
  case VisibilityNotify: return "VisibilityChange";
  case CreateNotify: return "CreateNotify";
  case DestroyNotify: return "DestroyNotify";
  case UnmapNotify: return "UnmapNotify";
  case MapNotify: return "MapNotify";
  case ReparentNotify: return "ReparentNotify";
  case ConfigureNotify: return "ConfigureNotify";
  case GravityNotify: return "GravityNotify";
  case ClientMessage: return "ClientMessage";
  case CirculateNotify: return "CirculateNotify";
  case MapRequest: return "MapRequest";
  case ConfigureRequest: return "ConfigureRequest";
  case ResizeRequest: return "ResizeRequest";
  case CirculateRequest: return "CirculateRequest";
  case PropertyNotify : return "PropertyNotify";
  case ColormapNotify : return "ColormapNotify";
  default: return "Unknown";
  }
}

char* debug_event_mode(XEvent* ev)
{
  int mode = (ev->type == FocusIn || ev->type == FocusOut ? ev->xfocus.mode : ev->xcrossing.mode);
  switch (mode) {
  case NotifyNormal: return "Normal";
  case NotifyGrab:   return "From Grab";
  case NotifyUngrab: return "From Ungrab";
  case NotifyWhileGrabbed: return "During Grab";
  default: return "Unknown mode";
  }
}

char* debug_event_detail(XEvent* ev)
{
  int detail = (ev->type == FocusIn || ev->type == FocusOut ? ev->xfocus.detail : ev->xcrossing.detail);
  switch (detail) {
  case NotifyAncestor: return "Ancestor";
  case NotifyVirtual: return "Virtual";
  case NotifyInferior:   return "Inferior";
  case NotifyNonlinear: return "Nonlinear";
  case NotifyNonlinearVirtual: return "Nonlinear virtual";
  case NotifyPointer: return "Pointer";
  case NotifyPointerRoot: return "Pointer root";
  case NotifyDetailNone: return "None";
  default: return "Unknown detail";
  }
}

int debug_count = 0;
int debug_dirty = 0;

void debug_display_event(XEvent* ev)
{
  if (!gwmlog) return;
  switch (ev->type) {
  case FocusIn:     fprintf(gwmlog, "X: FocusIn  %ld  %s, %s.\n", ev->xfocus.window, debug_event_mode(ev), debug_event_detail(ev)); fflush(gwmlog); debug_dirty = 1; break;
  case FocusOut:    fprintf(gwmlog, "X: FocusOut %ld  %s, %s.\n", ev->xfocus.window, debug_event_mode(ev), debug_event_detail(ev)); fflush(gwmlog); debug_dirty = 1; break;
  case EnterNotify: fprintf(gwmlog, "X: Enter    %ld  %s, %s.\n", ev->xcrossing.window, debug_event_mode(ev), debug_event_detail(ev)); fflush(gwmlog); debug_dirty = 1; break;
  case LeaveNotify: fprintf(gwmlog, "X: Leave    %ld  %s, %s.\n", ev->xcrossing.window, debug_event_mode(ev), debug_event_detail(ev)); fflush(gwmlog); debug_dirty = 1; break;
  }
  if (debug_dirty && !XPending(dpy)) {
    fprintf(gwmlog, "----- %d -----\n", ++debug_count);
    fflush(gwmlog);
    debug_dirty = 0;
  }
}

/* The main loop of GWM:
 * 	- wait for an event
 * 	- look if it is for the hook (X window) of a wob
 * 	  (or the client of a window), then dispatch it to the concerned
 * 	  wob
 * 	- if it is a new window being mapped, decorate it
 * 	- the server is grabbed, redirect events to grabbing wob
 * 
 * If blocking is 1, the loop never ends, whereas if it is 0, it returns as
 * soons as the queue is empty (via the XPending call)
 */
void GWM_ProcessEvents(int blocking)
{
  XEvent evt;
  Decoration* wob;
  ClientWindow* cw;
  ScreenContext* currscr = Context;
  GwmEvent* old_gwmevent;
  int s = 0, d;
  Time newtime;

  old_gwmevent = gwmevent_current;
  gwmevent_current = 0;
  if (GWM_stdin) {
    handle_stdin_check_prompt();
    s = GWM_AwaitInput(0);
  }
  while (blocking || XPending(dpy) || s > 0 || NextTimerDelay() == 0) {

    if (GWM_stdin) {
      if (blocking && !XPending(dpy)) {
        s = GWM_AwaitInput(NextTimerDelay());
      } else {
        s = GWM_AwaitInput(0);
      }
      while (s == 1) {
        handle_stdin_char();
        s = GWM_AwaitInput(0);
      }
      s = 0;
    }

    d = NextTimerDelay();
    if (d == 0) {
      ProcessTimerEvents();
      d = NextTimerDelay();
    }
    if (!XPending(dpy)) {
      if (blocking && d != 0)
        s = GWM_AwaitInput(d);
      continue;
    }

    XNextEvent(dpy, &evt);
    //debug_display_event(&evt);

    newtime = fetch_event_time(&evt);
    if (newtime)
      GWMTime = newtime;

    if ((wob = LookUpDeco(evt.xany.window))) {
      if (Fsm::ServerGrabbed()) {
        wob = Fsm::GetGrabTarget(wob, &evt);
        if (wob) {
          SetContext(wob->Screen());
          wob->EventHandler(&evt);
        }
      } else {
        SetContext(wob->Screen());
        wob->EventHandler(&evt);
      }
    } else if ((cw = LookUpClient(evt.xany.window))) {
      SetContext(cw->Client()->Screen());
      cw->EventHandler(&evt);
    } else if (evt.xany.type == MappingNotify) {
      XRefreshKeyboardMapping(&evt.xmapping);
      if (evt.xmapping.request == MappingModifier)
        keysym_to_keycode_modifier_mask(0, 0);  // reset mask
    } else if (evt.xany.type == ColormapNotify) {
      Update_colormap_in_colormap_windows_list((XColormapEvent*) &evt);
    }
  }
  SetContext(currscr);
  gwmevent_current = old_gwmevent;
}

extern int EventPat_latest_motion(XEvent* ret, Window win);

SCM GWM_AwaitPointerEvent(int rest)
{
  XEvent evt;
  Decoration* wob;
  ClientWindow* cw;
  ScreenContext* currscr = Context;
  GwmEvent* old_gwmevent;
  int end, d, s = 0;
  int unlim = (rest == -1);
  Time newtime;

  end = gwm_proper_time() + rest;
  old_gwmevent = gwmevent_current;
  gwmevent_current = 0;
  if (GWM_stdin)
    handle_stdin_check_prompt();
  while (unlim || rest > 0) {

    if (GWM_stdin) {
      if (!XPending(dpy)) {
        d = NextTimerDelay();
        s = GWM_AwaitInput((d >= 0 && (d < rest || unlim)) ? d : rest);
      } else {
        s = GWM_AwaitInput(0);
      }
      while (s == 1) {
        handle_stdin_char();
        s = GWM_AwaitInput(0);
      }
      s = 0;
    }

    d = NextTimerDelay();
    if (d == 0) {
      ProcessTimerEvents();
      d = NextTimerDelay();
    }
    if (!XPending(dpy)) {
      if (!unlim) rest = end - gwm_proper_time();
      s = GWM_AwaitInput((d >= 0 && (d < rest || unlim)) ? d : rest);
      if (!XPending(dpy)) {
        if (!unlim) rest = end - gwm_proper_time();
        continue;
      }
    }

    XNextEvent(dpy, &evt);
    //debug_display_event(&evt);

    newtime = fetch_event_time(&evt);
    if (newtime)
      GWMTime = newtime;

    if (evt.xany.type == MotionNotify) {
      EventPat_latest_motion(&evt, evt.xmotion.window);
      SetContext(currscr);
      gwmevent_current = old_gwmevent;
      return event2scm(new WlEvent(&evt, 0));
    } else if (evt.xany.type == ButtonPress ||
               evt.xany.type == ButtonRelease) {
      if (LookUpClient(evt.xany.window))  // Inner client - event is grabbed
        XAllowEvents(dpy, AsyncPointer, CurrentTime);
      SetContext(currscr);
      gwmevent_current = old_gwmevent;
      return event2scm(new WlEvent(&evt, 0));
    } else if ((wob = LookUpDeco(evt.xany.window))) {
      if (Fsm::ServerGrabbed()) {
        wob = Fsm::GetGrabTarget(wob, &evt);
        if (wob) {
          SetContext(wob->Screen());
          wob->EventHandler(&evt);
        }
      } else {
        SetContext(wob->Screen());
        wob->EventHandler(&evt);
      }
    } else if ((cw = LookUpClient(evt.xany.window))) {
      SetContext(cw->Client()->Screen());
      cw->EventHandler(&evt);
    } else if (evt.xany.type == MappingNotify) {
      XRefreshKeyboardMapping(&evt.xmapping);
      if (evt.xmapping.request == MappingModifier)
        keysym_to_keycode_modifier_mask(0, 0);  // reset mask
    } else if (evt.xany.type == ColormapNotify) {
      Update_colormap_in_colormap_windows_list((XColormapEvent*) &evt);
    }
    if (!unlim) rest = end - gwm_proper_time();
  }
  SetContext(currscr);
  gwmevent_current = old_gwmevent;
  return SCM_BOOL_F;
}

/* re-init pointerroot */
void GWM_re_init_PointerRoot()
{
  ScreenContext* scr;
  FOR_ALL_SCREENS(scr)
    XSelectInput(dpy, scr->Root(), 0);
  XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
}

void init_hostname()
{
  char buf[256];
  int maxlen = 256;
  int len;
#ifdef HAVE_GETHOSTNAME
  buf[0] = '\0';
  (void) gethostname (buf, maxlen);
  buf [maxlen - 1] = '\0';
  len = strlen(buf);
#else
  struct utsname name;
  uname (&name);
  len = strlen (name.nodename);
  if (len >= maxlen) len = maxlen - 1;
  strncpy (buf, name.nodename, len);
  buf[len] = '\0';
#endif
  GWM_host_name = scm_makfrom0str(buf);
  scm_gc_protect_object(GWM_host_name);
}

/*
 * GWM_Initialize
 * initialize:
 * 	X
 * 	Guile
 * 	internal variables
 */
int GWM_Initialize()
{
  XSetWindowAttributes attributes;
  ScreenContext* scr;

  GWM_SignalsInit();
  GWM_init_guile();
  RegisterScreens();
  SetUpDefaults();
  SetDefaultScreen();
  cmd_screen = Context;
  init_hostname(); /* initialize GWM_host_name */

  /* Manage Windows */
  if (GWM_WidgetMode)
    attributes.event_mask = EnterWindowMask | LeaveWindowMask | FocusChangeMask;
  else
    attributes.event_mask = KeyPressMask | ButtonPressMask | ButtonReleaseMask
      | EnterWindowMask | LeaveWindowMask | FocusChangeMask | SubstructureRedirectMask;
  XSync(dpy, False);
  XSetErrorHandler(NoXError);
  FOR_ALL_SCREENS(scr) {
    ErrorStatus = 0;
    XChangeWindowAttributes(dpy, scr->Root(), CWEventMask, &attributes);
    XSync(dpy, False);
    if (ErrorStatus) {
      fprintf(stderr,
              "GWM: cannot get control of windows on screen #%d!\n%s\n",
              scr->ScreenNum(),
              "Maybe another window manager is running on the display?");
      if (GWM_RetryStart) {
        fprintf(stderr, "Retrying... kill your other window manager!\n");
        do {
          sleep(1);
          ErrorStatus = 0;
          XChangeWindowAttributes(dpy, scr->Root(),
                                  CWEventMask, &attributes);
          XSync(dpy, False);
        } while(ErrorStatus);
        fprintf(stderr, "OK for screen %d\n", scr->ScreenNum());
      } else {
        exit(1);
      }
    }
  }
  if (!GWM_WidgetMode)
    GWM_re_init_PointerRoot();
  XSetErrorHandler(XError);
  return OK;
}

int gwm_init_profile()
{
  SCM file;
  /* first time, load the user file */
  file = scm_sys_search_load_path(scm_makfrom0str(GWM_user_profile_name));
  if (file != SCM_BOOL_F &&
      gwm_call_with_catch((scm_t_catch_body) scm_primitive_load,
                          (void*) file))
    return 1;
  else
    return 0;
}

/* CloseEverything
 * n = 	0 normal (end)
 * 	1 error (we can still print messages)
 * 	-1 fatal error (connection closed)
 */

void gwm_end(int n)
{
  ScreenContext* scr;
  if (n >= 0 && !GWM_is_starting) {
    XSetErrorHandler(NoXError);
    if (!GWM_WidgetMode)
      GWM_re_init_PointerRoot();
    XSync(dpy, True);
    if (!GWM_is_ending) {
      GWM_is_ending = 1;
      if (GWM_rubber_feedback)
        UnDrawRubber();
      if (Fsm::ServerGrabbed())
        Fsm::ClearGrabs();
      FOR_ALL_SCREENS(scr)
        scr->Close();
    }
    XCloseDisplay(dpy);
    if (GWM_stdin)
      rl_callback_handler_remove();
  }
  exit(n);
}

/* signals management
 */

static int GWM_in_fatal_error;

void unblock_signal(int sig)
{
#ifdef HAVE_SIGACTION
  sigset_t set;
  sigemptyset (&set);
  sigaddset (&set, sig);
  sigprocmask (SIG_UNBLOCK, &set, NULL);  
#endif
}

/* We try to handle bus errors gracefully, i.e. by trying to trigger a
 * gwm_error and going on, but aborting if we get one more
 */

void GWM_FatalSignalHandler(int sig)
{
  if (!GWM_in_fatal_error) {
    GWM_in_fatal_error = 1;
    XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    XSync(dpy, True);
    fprintf(stderr,
	    "\007\nGWM: Fatal error due to UNIX signal # %d -- Aborting\n",
	    sig);
  }
  exit(sig);
}

/* Try to handle bus errors as normal errors, execpt that:
 * 	- if 2 bus errors come within 1 second, abort.
 * 	- if we were ending, just end, (in case the end code is faulty)
 * 	- ditto for restarting
 */

void GWM_BusErrorSignalHandler(int sig)
{
  int             current_time;
  static int      aborting;

  current_time = gwm_proper_time();
  if (current_time - GWM_time_of_last_bus_error < 1000 &&
      current_time - GWM_time_of_last_bus_error >= 0 &&
      GWM_time_of_last_bus_error != 0) {
    if (aborting || GWM_is_ending) {
      exit(-1);
    } else {
      aborting = 1;
      GWM_is_ending = 1;
      fprintf(stderr,
              "\007\nGWM: Internal error: bus error -- restarting\n");
      XCloseDisplay(dpy);
      rl_free_line_state ();
      rl_cleanup_after_signal();
      rl_deprep_terminal ();
      unblock_signal(sig);
      execvp(GWM_argv[0], GWM_argv);
    }
  }
  GWM_time_of_last_bus_error = current_time;
  if (GWM_restart_argv) {
    XCloseDisplay(dpy);
    rl_free_line_state ();
    rl_cleanup_after_signal();
    rl_deprep_terminal ();
    unblock_signal(sig);
    execvp(GWM_restart_argv[0], GWM_restart_argv);
  }
  if (GWM_is_ending)
    gwm_end(-1);
  signal(sig, GWM_BusErrorSignalHandler);
  gwm_internal_error(0, "bus error");
}

void GWM_InteruptSignalHandler(int sig)
{
  rl_free_line_state ();
  rl_cleanup_after_signal();
  unblock_signal(sig);
  signal(SIGINT, GWM_InteruptSignalHandler);
  global_handle_stdin_shown = 0;
  scm_newline(scm_current_output_port());
  gwm_misc_error(0, "Interrupt.", 0);
}

struct ChildEle {
  pid_t id;
  int dead;
  int status;
  ChildEle* next;
  ChildEle(int pid) { id = (pid_t) pid; dead = 0; next = 0; };
};

static ChildEle* ChildQueue = 0;

void ChildDeathHandler(int sig)
{
  int status;
  ChildEle* ele;
#ifdef HAVE_WAIT3
  pid_t id = wait3(&status, WNOHANG, 0);
#else
  pid_t id = wait(&status);
#endif
  signal(SIGCLD, ChildDeathHandler);
  for (ele=ChildQueue; ele; ele=ele->next)
    if (ele->id == id) {
      ele->dead = 1;
      ele->status = status;
      break;
    }
}

void GWM_SignalsInit()
{
  signal(SIGHUP, SIG_IGN);
  signal(SIGINT, GWM_InteruptSignalHandler);
  signal(SIGQUIT, gwm_end);
  signal(SIGTERM, gwm_end);
  signal(SIGCLD, ChildDeathHandler);
  signal(SIGFPE, GWM_FatalSignalHandler);
  signal(SIGILL, GWM_BusErrorSignalHandler);
#ifdef SIGBUS
  signal(SIGBUS, GWM_BusErrorSignalHandler);
#endif
  signal(SIGSEGV, GWM_BusErrorSignalHandler);
#ifdef SIGSYS
  signal(SIGSYS, GWM_FatalSignalHandler);
#endif
}

SCM_DEFINE(gwm_sleep, "sleep", 1, 0, 0,
           (SCM sec),
           "Wait (at least) the given number of seconds (a real number). Always"
           "returns 0 for compatibility with the standard sleep function.")
{
  if (!gh_number_p(sec))
    gwm_wrong_type_arg(s_gwm_sleep, 1, sec, "number");
  GWM_InternSleep((int) (gh_scm2double(sec)*1000));
  return gh_int2scm(0);
}

SCM_DEFINE(gwm_usleep, "usleep", 1, 0, 0,
           (SCM usec),
           "Waits (at least) the given number of microseconds (actually rounded"
           "to a whole number of milliseconds).")
{
  int rest;
  if (!scm_is_integer(usec))
    gwm_wrong_type_arg(s_gwm_usleep, 1, usec, "integer");
  rest = gh_scm2int(usec);
#ifdef HAVE_USLEEP
  if (rest < 2000)
    usleep(rest);
  else 
#endif
    GWM_InternSleep(rest / 1000);
  return gh_int2scm(0);
}

SCM_DEFINE(gwm_waitpid, "waitpid", 1, 1, 0,
           (SCM pid, SCM options),
           "")
{
  int s, res;
  int status, ioptions;
  ChildEle* child;
  must_be_number(s_gwm_waitpid, pid, 1);
  if (SCM_UNBNDP(options))
    ioptions = 0;
  else {
    must_be_number(s_gwm_waitpid, options, 2);
    ioptions = scm_to_int(options);
  }
  if (!(ioptions & WNOHANG)) {
    child = new ChildEle(scm_to_int(pid));
    child->next = ChildQueue;
    ChildQueue = child;
    res = waitpid(scm_to_int(pid), &status, WNOHANG | ioptions);
    if (res == -1) {
      // We are going to return
      ChildQueue = child->next;
      delete child;
      if (errno != ECHILD)
        scm_syserror(s_gwm_waitpid);
      else
        errno = 0;
    } else {
      // Start looping
      if (GWM_stdin)
        handle_stdin_check_prompt();
      if (XPending(dpy))
        GWM_ProcessEvents(0);
      while (!child->dead) {
        s = GWM_AwaitInput(100);
        if (s > 0)
          GWM_ProcessEvents(0);
      } 
      status = child->status;
      res = 0;
      ChildQueue = child->next;
      delete child;
    }
  } else {
    res = waitpid(scm_to_int(pid), &status, ioptions);
  }
  return scm_cons (gh_int2scm(0L + res), gh_int2scm(0L + status));
}

SCM_DEFINE (gwm_version, "gwm-version", 0, 0, 0, 
            (),
	    "Returns the current Gwm version as a string.")
{
  return scm_makfrom0str(GWM_VERSION);
}

/*
 *  initialize X and Context
 */
void GWM_OpenDisplay(char* display)
{
  GWM_DisplayName = XDisplayName(display);
  if ((dpy = XOpenDisplay(display)) == NULL) {
    fprintf(stderr, "\007GWM: Cannot XOpenDisplay on %s\n",
            XDisplayName(display));
    exit(1);
  }
  GWM_ShapeExtension = XShapeQueryExtension(dpy, &GWM_ShapeEventBase,
                                            &GWM_ShapeErrorBase);
  if (GWM_Synchronize)
    XSynchronize(dpy, 1);
  deco_context = XUniqueContext();
  client_context = XUniqueContext();
  XSetErrorHandler(XError);
}

/* options handling
 */

/* add .:$HOME:$HOME/gwm: before built-in-path (INSTALL_DIR) and returns it  */

char* gwm_fix_path(char* built_in_path)
{
    char *home = getenv("HOME");
    char *path = new char[strlen(built_in_path) + 6 + strlen(GWM_APP_name)
                         + 2 * (home ? strlen(home) : 0)];

    strcpy(path, ".:");
    if (home) {
	strcat(path, home);
	strcat(path, ":");
	strcat(path, home);
	strcat(path, "/");
	strcat(path, GWM_APP_name);
	strcat(path, ":");
    }
    strcat(path, built_in_path);
    return path;
}

char *
path_append(char* old_path, char* new_path)
{
    char* path;
    switch(*new_path){
    case '+': case '-':		/* append & prepend */
      path = new char[strlen(old_path) + strlen(new_path) + 1];
      strcpy(path, (*new_path == '+' ? old_path : new_path+1));
      strcat(path, ":");
      strcat(path, (*new_path == '+' ? new_path+1 : old_path));
      break;
    default:			/* replace */
      path = new char[strlen(new_path) + 1];
      strcpy(path, new_path);
    }
    delete [] old_path;
    return path;
}

void usage()
{
    fprintf(stderr, "Usage:   gwm OPTIONS [host:display]\n");
    fprintf(stderr, "Normal options are:\n");	
    fprintf(stderr, "    -f file      use file instead of " USER_PROFILE "\n");
    fprintf(stderr, "    -p path      use path as Gwm path\n");	
    fprintf(stderr, "    -p +path     appends to Gwm path\n");	
    fprintf(stderr, "    -p -path     prepends to Gwm path\n");	
    fprintf(stderr, "    -d display   manage display\n");
    fprintf(stderr, "    -x screens   do not manage screens, e.g. -x3,2,5\n");
    fprintf(stderr, "    -k pid       send signal (def. SIGALRM) to pid when init is done\n");
    fprintf(stderr, "    -K signal    number of signal to send for -k option\n");
    fprintf(stderr, "    -1    manages only 1 screen, the default one\n");
    fprintf(stderr, "    -r    retries till it can manage screens\n");
    fprintf(stderr, "    -b    do not read Guile commands from stdin\n");
    fprintf(stderr, "    -n    suppress prompt when reading from stdin\n");
    fprintf(stderr, "    -q    quiet: do not print startup banner\n");
    fprintf(stderr, "Debug options are:\n");	
    fprintf(stderr, "    -I    input focus remains PointerRoot\n");
    fprintf(stderr, "    -F    no freeze: never freeze server during grabs\n");
    fprintf(stderr, "    -M    map all windows, including unmapped ones, on startup\n");	
    fprintf(stderr, "    -S    synchronize X calls\n");	
    fprintf(stderr, "Help options are:\n");	
    fprintf(stderr, "    -V    print Gwm version and exit\n");
    fprintf(stderr, "    -h,-? print this help and exit\n");
    fprintf(stderr, "\nGwm path is %s\n", GWM_path);
    fprintf(stderr, "Built-in path was  %s\n", DEFAULT_GWMPATH);
}

void GWM_getopts(int argc, char** argv)
{
    extern int      optind;
    extern char    *optarg;
    char 	   *display_name = 0;
    char* s;
    int             c, errflag = 0;

    GWM_user_profile_name = USER_PROFILE;

    /* initialize paths (put .:$HOME before built-ins) */
    GWM_path = gwm_fix_path(DEFAULT_GWMPATH);
    if ((s = (char *) getenv(WLPROFILE_USER_VARIABLE)) && (s[0] != '\0'))
	GWM_user_profile_name = s;	     
    if ((s = (char *) getenv(GWMPATH_SHELL_VARIABLE)) && (s[0] != '\0'))
	GWM_path = s;
    if ((s = (char *) getenv("GWM_MONOSCREEN")) && (s[0] != '\0'))
	GWM_Monoscreen = 1;

    while ((c = getopt(argc, argv, "f:p:d:x:k:K:1rbnql:IFMSWVh?")) != EOF) {
	switch ((char) c) {
	case 'b':
	    GWM_stdin = 0;
	    break;
	case 'n':
	    GWM_prompt = 0;
	    break;
	case 'l':
	    gwmlog = fopen(optarg, "w");
	    break;
	case 'I':
	    GWM_No_set_focus = 1;
	    break;
	case 'F':
            GWM_FreezeServer = 0;
	    break;
	case 'M':		/* map all windows found on startup */
	    GWM_Mapall = 1;
	    break;
	case 'S':		/* synchronize X calls */
	    GWM_Synchronize = 1;
	    break;
	case 'W':
            GWM_WidgetMode = 1;
	    break;
	case 'r':		/* retry on StructureRedirect */
	    GWM_RetryStart = 1;
	    break;
	case 'f':
	    GWM_user_profile_name = optarg;
	    break;
	case 'p':
	    GWM_path = path_append(GWM_path, optarg);
	    break;
	case 'd':
	    display_name = optarg;
	    break;
	case 'x':
	    GWM_ScreensNotManaged = optarg;
	    break;
	case '1':
	    GWM_Monoscreen = 1;
	    break;
	case 'q':
            GWM_Quiet = 1;
	    break;
	case 'k':
	    GWM_kill_pid = atoi(optarg);
	    break;
	case 'K':
	    GWM_kill_pid_sig = atoi(optarg);
	    break;
	case 'V':
            fprintf(stderr, "%s\n", GWM_VERSION);
            exit(0);
	case 'h':		/* usage */
	case '?':
            fprintf(stderr, "%s\n", GWM_VERSION);
            usage();
            exit(0);
	default:
            fprintf(stderr, "%s\n", GWM_VERSION);
            usage();
            exit(1);
	}
    }

    if (!GWM_kill_pid_sig)
	GWM_kill_pid_sig = SIGALRM;

    if(!GWM_Quiet)
      printf("%s\n", GWM_VERSION);

    GWM_OpenDisplay(optind < argc ? argv[optind] : display_name);
}

/* parses a comma-separated list of numbers (string) and pokes 1 in the list
 * if comprised beetween 0 and max_n
 */
void mark_numbers_in_list(char* string, ScreenContext** list)
{
    int n;
    while (string && *string) {
	n = atoi(string);
	if (*string >= '0' && *string <= '9'
	    && n >= 0 && n < GWM_ScreenCount)
	    list[n] = (ScreenContext*) 1;
	if ((string = strchr(string, ',')))
	    string++;
	else
	    return;
    }
}

/* set up the list of managed screens: GWMManagedScreens */
void RegisterScreens()   
{
    int screen_num;
    int number_of_managed_screens = 0;

    GWM_ScreenCount = ScreenCount(dpy);
    GWMManagedScreens = new ScreenContext*[GWM_ScreenCount];
    for (screen_num = 0; screen_num < GWM_ScreenCount; screen_num++)
      GWMManagedScreens[screen_num] = NULL;
    /* parse list of non-managed screens and pokes them */
    mark_numbers_in_list(GWM_ScreensNotManaged, GWMManagedScreens);

    /* poke 1 in used screens, 0 in unused  and count them
     * manage only DefaultScreen if we are in Monoscreen mode
     */
    for (screen_num = 0; screen_num < GWM_ScreenCount; screen_num++) {
	if (GWMManagedScreens[screen_num]) {
	    GWMManagedScreens[screen_num] = NULL;
	} else if ((!GWM_Monoscreen) || screen_num == DefaultScreen(dpy)) {
	    GWMManagedScreens[screen_num] = (ScreenContext*) 1;
	    number_of_managed_screens++;
	}
    }

    /* (abort if none left) */
    if (!number_of_managed_screens) {
      fprintf(stderr, "\007GWM: No screens to manage, aborting\n");
      exit(1);
    }

    /* allocates and registers the managed screens */
    for (screen_num = 0; screen_num < GWM_ScreenCount; screen_num++) {
	if (GWMManagedScreens[screen_num]) {
	    GWMManagedScreens[screen_num] = new ScreenContext(screen_num);
	}
    }
}


void init_scm_gwm()
{
#include "gwm.x"
}
