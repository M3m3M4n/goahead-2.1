/*
 * main.c -- Main program for the GoAhead WebServer
 *
 * Copyright (c) GoAhead Software Inc., 1995-2000. All Rights Reserved.
 *
 * See the file "license.txt" for usage and redistribution license requirements
 *
 * $Id: main.c,v 1.3 2002/07/29 15:28:33 bporter Exp $
 */

/******************************** Description *********************************/

/*
 *	Main program for the GoAhead WebServer. This is a demonstration
 *	program to initialize and configure the web server.
 */

/********************************* Includes ***********************************/

#include	<direct.h>
#include	<windows.h>
#include	<winuser.h>
#include	<process.h>

#include	"../wsIntrn.h"

#ifdef WEBS_SSL_SUPPORT
#include	"../websSSL.h"
#endif

#ifdef USER_MANAGEMENT_SUPPORT
#include	"../um.h"
void		formDefineUserMgmt(void);
#endif

/********************************** Defines ***********************************/

#define		IDM_ABOUTBOX		0xF200
#define		IDS_ABOUTBOX		"AboutBox"
#define		IDC_VERSION			101
#define		IDC_BUILDDATE		102
#define		IDC_DISMISS			103
#define		SOCK_DFT_SVC_TIME	20

/*********************************** Locals ***********************************/
/*
 *	Change configuration here
 */

static HWND		hwnd;							/* Main Window handle */
static HWND		hwndAbout;						/* About Window handle */
static char_t	*title = T("GoAhead WebServer");/* Window title */
static char_t	*name = T("gowebs");			/* Window name */
static char_t	*rootWeb = T("web");			/* Root web directory */
static char_t	*password = T("");				/* Security password */
static int		port = 80;						/* Server port */
static int		retries = 5;					/* Server port retries */
static int		finished;						/* Finished flag */
static int		sockServiceTime;				/* in milliseconds */

/****************************** Forward Declarations **************************/

static int		initWebs();
static long		CALLBACK websWindProc(HWND hwnd, unsigned int msg, 
				unsigned int wp, long lp);
static long		CALLBACK websAboutProc(HWND hwnd, unsigned int msg, 
				unsigned int wp, long lp);
static int		registerAboutBox(HINSTANCE hInstance);
static int		createAboutBox(HINSTANCE hInstance, HWND hwnd);
static int		aspTest(int eid, webs_t wp, int argc, char_t **argv);
static WPARAM	checkWindowsMsgLoop();
static void		formTest(webs_t wp, char_t *path, char_t *query);
static int		windowsInit(HINSTANCE hinstance);
static int		windowsClose(HINSTANCE hinstance);
static int		websHomePageHandler(webs_t wp, char_t *urlPrefix,
					char_t *webDir, int arg, char_t *url, char_t *path,
					char_t *query);
extern void		defaultErrorHandler(int etype, char_t *msg);
extern void		defaultTraceHandler(int level, char_t *buf);
static void		printMemStats(int handle, char_t *fmt, ...);
static void		memLeaks();

static LPWORD	lpwAlign(LPWORD);
static int		nCopyAnsiToWideChar(LPWORD, LPSTR);
static void		centerWindowOnDisplay(HWND hwndCenter);

static unsigned char	*tableToBlock(char **table);

/*********************************** Code *************************************/
/*
 *	WinMain -- entry point from Windows
 */

int APIENTRY WinMain(HINSTANCE hinstance, HINSTANCE hprevinstance,
						char *args, int cmd_show)
{
	WPARAM	rc;

/*
 *	Initialize the memory allocator. Allow use of malloc and start 
 *	with a 60K heap.  For each page request approx 8KB is allocated.
 *	60KB allows for several concurrent page requests.  If more space
 *	is required, malloc will be used for the overflow.
 */
	bopen(NULL, (60 * 1024), B_USE_MALLOC);

/* 
 *	Store the instance handle (used in socket.c)
 */

	if (windowsInit(hinstance) < 0) {
		return FALSE;
	}

/*
 *	Initialize the web server
 */
	if (initWebs() < 0) {
		return FALSE;
	}

#ifdef WEBS_SSL_SUPPORT
	websSSLOpen();
#endif

/*
 *	Basic event loop. SocketReady returns true when a socket is ready for
 *	service. SocketSelect will block until an event occurs. SocketProcess
 *	will actually do the servicing.
 */
	while (!finished) {
		if (socketReady(-1) || socketSelect(-1, sockServiceTime)) {
			socketProcess(-1);
		}
		emfSchedProcess();
		websCgiCleanup();
		if ((rc = checkWindowsMsgLoop()) != 0) {
			break;
		}
	}

#ifdef WEBS_SSL_SUPPORT
	websSSLClose();
#endif

/*
 *	Close the User Management database
 */
#ifdef USER_MANAGEMENT_SUPPORT
	umClose();
#endif

/*
 *	Close the socket module, report memory leaks and close the memory allocator
 */
	websCloseServer();
	socketClose();

/*
 *	Free up Windows resources
 */
	windowsClose(hinstance);

#ifdef B_STATS
	memLeaks();
#endif
	bclose();
	return rc;
}

/******************************************************************************/
/*
 *	Initialize the web server.
 */

static int initWebs()
{
	struct hostent	*hp;
	struct in_addr	intaddr;
	char			*cp;
	char			host[64], dir[128];
	char_t			dir_t[128];
	char_t			wbuf[256];

/*
 *	Initialize the socket subsystem
 */
	socketOpen();

/*
 *	Initialize the User Management database
 */
#ifdef USER_MANAGEMENT_SUPPORT
	umOpen();
	umRestore(T("umconfig.txt"));
#endif

/*
 *	Define the local Ip address, host name, default home page and the 
 *	root web directory.
 */
	if (gethostname(host, sizeof(host)) < 0) {
		error(E_L, E_LOG, T("Can't get hostname"));
		return -1;
	}
	if ((hp = gethostbyname(host)) == NULL) {
		error(E_L, E_LOG, T("Can't get host address"));
		return -1;
	}
	memcpy((void *) &intaddr, (void *) hp->h_addr_list[0],
		(size_t) hp->h_length);

/*
 *	Set ../web as the root web. Modify this to suit your needs
 */
	getcwd(dir, sizeof(dir)); 
	for (cp = dir; *cp; cp++) {
		if (*cp == '\\')
			*cp = '/';
	}
	if (cp = strrchr(dir, '/')) {
		*cp = '\0';
	}
	ascToUni(dir_t, dir, sizeof(dir_t));
	gsprintf(wbuf, T("%s/%s"), dir_t, rootWeb);
	websSetDefaultDir(wbuf);
	cp = inet_ntoa(intaddr);
	ascToUni(wbuf, cp, min(strlen(cp) + 1, sizeof(wbuf)));
	websSetIpaddr(wbuf);
	ascToUni(wbuf, hp->h_name, min(strlen(hp->h_name) + 1, sizeof(wbuf)));
	websSetHost(wbuf);

/*
 *	Configure the web server options before opening the web server
 */
	websSetDefaultPage(T("default.asp"));
	websSetPassword(password);

/* 
 *	Open the web server on the given port. If that port is taken, try
 *	the next sequential port for up to "retries" attempts.
 */
	websOpenServer(port, retries);

/*
 * 	First create the URL handlers. Note: handlers are called in sorted order
 *	with the longest path handler examined first. Here we define the security 
 *	handler, forms handler and the default web page handler.
 */
	websUrlHandlerDefine(T(""), NULL, 0, websSecurityHandler, 
		WEBS_HANDLER_FIRST);
	websUrlHandlerDefine(T("/goform"), NULL, 0, websFormHandler, 0);
	websUrlHandlerDefine(T("/cgi-bin"), NULL, 0, websCgiHandler, 0);
	websUrlHandlerDefine(T(""), NULL, 0, websDefaultHandler, 
		WEBS_HANDLER_LAST); 

/*
 *	Now define two test procedures. Replace these with your application
 *	relevant ASP script procedures and form functions.
 */
	websAspDefine(T("aspTest"), aspTest);
	websFormDefine(T("formTest"), formTest);
/*
 *	Create the Form handlers for the User Management pages
 */
#ifdef USER_MANAGEMENT_SUPPORT
	formDefineUserMgmt();
#endif

/*
 *	Create a handler for the default home page
 */
	websUrlHandlerDefine(T("/"), NULL, 0, websHomePageHandler, 0); 

/* 
 *	Set the socket service timeout to the default
 */
	sockServiceTime = SOCK_DFT_SVC_TIME;				

	return 0;
}


/******************************************************************************/
/*
 *	Create a taskbar entry. Register the window class and create a window
 */

static int windowsInit(HINSTANCE hinstance)
{
	WNDCLASS  		wc;						/* Window class */
	HMENU			hSysMenu;

	emfInstSet((int) hinstance);

	wc.style		 = CS_HREDRAW | CS_VREDRAW;
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
	wc.hCursor		 = LoadCursor(NULL, IDC_ARROW);
	wc.cbClsExtra	 = 0;
	wc.cbWndExtra	 = 0;
	wc.hInstance	 = hinstance;
	wc.hIcon		 = NULL;
	wc.lpfnWndProc	 = (WNDPROC) websWindProc;
	wc.lpszMenuName	 = wc.lpszClassName = name;
	if (! RegisterClass(&wc)) {
		return -1;
	}

/*
 *	Create a window just so we can have a taskbar to close this web server
 */
	hwnd = CreateWindow(name, title, WS_MINIMIZE | WS_POPUPWINDOW,
		CW_USEDEFAULT, 0, 0, 0, NULL, NULL, hinstance, NULL);
	if (hwnd == NULL) {
		return -1;
	}

/*
 *	Add the about box menu item to the system menu
 *	a_assert: IDM_ABOUTBOX must be in the system command range.
 */
	hSysMenu = GetSystemMenu(hwnd, FALSE);
	if (hSysMenu != NULL)
	{
		AppendMenu(hSysMenu, MF_SEPARATOR, 0, NULL);
		AppendMenu(hSysMenu, MF_STRING, IDM_ABOUTBOX, T("About WebServer"));
	}

	ShowWindow(hwnd, SW_SHOWNORMAL);
	UpdateWindow(hwnd);

	hwndAbout = NULL;
	return 0;
}

/******************************************************************************/
/*
 *	Close windows resources
 */

static int windowsClose(HINSTANCE hInstance)
{
	int nReturn;

	nReturn = UnregisterClass(name, hInstance);

	if (hwndAbout) {
		DestroyWindow(hwndAbout);
	}

	return nReturn;
}

/******************************************************************************/
/*
 *	Main menu window message handler.
 */

static long CALLBACK websWindProc(HWND hwnd, unsigned int msg, 
									unsigned int wp, long lp)
{
	switch (msg) {
		case WM_DESTROY:
			PostQuitMessage(0);
			finished++;
			return 0;

		case WM_SYSCOMMAND:
			if (wp == IDM_ABOUTBOX) {
				if (!hwndAbout) {
					createAboutBox((HINSTANCE) emfInstGet(), hwnd);
				}
				if (hwndAbout) {
					ShowWindow(hwndAbout, SW_SHOWNORMAL);
				}
			}
			break;
	}

	return DefWindowProc(hwnd, msg, wp, lp);
}

/******************************************************************************/
/*
 *	Check for Windows Messages
 */

WPARAM checkWindowsMsgLoop()
{
	MSG		msg;					/* Message block */

	if (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) {
		if (!GetMessage(&msg, NULL, 0, 0) || msg.message == WM_QUIT) {
			return msg.wParam;
		}

		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}

/******************************************************************************/
/*
 *	Windows message handler for the About Box
 */

static long CALLBACK websAboutProc(HWND hwndDlg, unsigned int msg, 
									unsigned int wp, long lp)
{
	long lResult;
	HWND hwnd;

	lResult = DefWindowProc(hwndDlg, msg, wp, lp);

	switch (msg) {
		case WM_CREATE:
			hwndAbout = hwndDlg;
			break;

		case WM_DESTROY:
			hwndAbout = NULL;
			break;

		case WM_COMMAND:
			if (wp == IDOK) {
				EndDialog(hwndDlg, 0);
				PostMessage(hwndDlg, WM_CLOSE, 0, 0);
			}
			break;

		case WM_INITDIALOG:
/*
 *			Set the version and build date values
 */
			hwnd = GetDlgItem(hwndDlg, IDC_VERSION);
			if (hwnd) {
				SetWindowText(hwnd, WEBS_VERSION);
			}

			hwnd = GetDlgItem(hwndDlg, IDC_BUILDDATE);
			if (hwnd) {
				SetWindowText(hwnd, __DATE__);
			}

			SetWindowText(hwndDlg, T("GoAhead WebServer"));
			centerWindowOnDisplay(hwndDlg);

			hwndAbout = hwndDlg;

			lResult = FALSE;
			break;
	}

	return lResult;
}

/******************************************************************************/
/*
 *	Registers the About Box class
 */

static int registerAboutBox(HINSTANCE hInstance)
{
	WNDCLASS  wc;

    wc.style = 0;
    wc.lpfnWndProc = (WNDPROC)websAboutProc;

    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = hInstance;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = GetStockObject(LTGRAY_BRUSH);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = IDS_ABOUTBOX;

    if (!RegisterClass(&wc)) {
		return 0;
	}

 	return 1;
}

/******************************************************************************/
/*
 *   Helper routine.  Take an input pointer, return closest
 *    pointer that is aligned on a DWORD (4 byte) boundary.
 */

static LPWORD lpwAlign(LPWORD lpIn)
{
	ULONG ul;

	ul = (ULONG) lpIn;
	ul +=3;
	ul >>=2;
	ul <<=2;
	return (LPWORD) ul;
}

/******************************************************************************/
/*
 *   Helper routine.  Takes second parameter as Ansi string, copies
 *    it to first parameter as wide character (16-bits / char) string,
 *    and returns integer number of wide characters (words) in string
 *    (including the trailing wide char NULL).
 */

static int nCopyAnsiToWideChar(LPWORD lpWCStr, LPSTR lpAnsiIn)
{
	int cchAnsi = lstrlen(lpAnsiIn);

	return MultiByteToWideChar(GetACP(), 
		MB_PRECOMPOSED, lpAnsiIn, cchAnsi, lpWCStr, cchAnsi) + 1;
}

/******************************************************************************/
/*
 *	Creates an About Box Window
 */

static int createAboutBox(HINSTANCE hInstance, HWND hwnd)
{
	WORD	*p, *pdlgtemplate;
	int		nchar;
	DWORD	lStyle;
	HWND	hwndReturn;

/* 
 *	Allocate some memory to play with  
 */
	pdlgtemplate = p = (PWORD) LocalAlloc(LPTR, 1000);

/*
 *	Start to fill in the dlgtemplate information.  addressing by WORDs 
 */

	lStyle = WS_DLGFRAME | WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN | DS_SETFONT;
	*p++ = LOWORD(lStyle);
	*p++ = HIWORD(lStyle);
	*p++ = 0;          /* LOWORD (lExtendedStyle) */
	*p++ = 0;          /* HIWORD (lExtendedStyle) */
	*p++ = 7;          /* Number Of Items	*/
	*p++ = 210;        /* x */
	*p++ = 10;         /* y */
	*p++ = 200;        /* cx */
	*p++ = 100;        /* cy */
	*p++ = 0;          /* Menu */
	*p++ = 0;          /* Class */

/* 
 *	Copy the title of the dialog 
 */
	nchar = nCopyAnsiToWideChar(p, "GoAhead WebServer");
	p += nchar;

/*	
 *	Font information because of DS_SETFONT
 */
	*p++ = 11;     /* point size */
	nchar = nCopyAnsiToWideChar(p, T("Arial Bold"));
	p += nchar;

/*
 *	Make sure the first item starts on a DWORD boundary
 */
	p = lpwAlign(p);

/*
 *	Now start with the first item (Product Identifier)
 */
	lStyle = SS_CENTER | WS_VISIBLE | WS_CHILD | WS_TABSTOP;
	*p++ = LOWORD(lStyle);
	*p++ = HIWORD(lStyle);
	*p++ = 0;			/* LOWORD (lExtendedStyle) */
	*p++ = 0;			/* HIWORD (lExtendedStyle) */
	*p++ = 10;			/* x */
	*p++ = 10;			/* y  */
	*p++ = 180;			/* cx */
	*p++ = 15;			/* cy */
	*p++ = 1;			/* ID */

/*
 *	Fill in class i.d., this time by name
 */
	nchar = nCopyAnsiToWideChar(p, TEXT("STATIC"));
	p += nchar;

/*
 *	Copy the text of the first item
 */
	nchar = nCopyAnsiToWideChar(p, 
		TEXT("GoAhead WebServer 2.1.3"));
	p += nchar;

/*
 *	Advance pointer over nExtraStuff WORD
 */
	*p++ = 0;  

/*
 *	Make sure the next item starts on a DWORD boundary
 */
	p = lpwAlign(p);

/*
 *	Next, the Copyright Notice.
 */
	lStyle = SS_CENTER | WS_VISIBLE | WS_CHILD | WS_TABSTOP;
	*p++ = LOWORD(lStyle);
	*p++ = HIWORD(lStyle);
	*p++ = 0;			/* LOWORD (lExtendedStyle) */
	*p++ = 0;			/* HIWORD (lExtendedStyle) */
	*p++ = 10;			/* x */
	*p++ = 30;			/* y  */
	*p++ = 180;			/* cx */
	*p++ = 15;			/* cy */
	*p++ = 1;			/* ID */

/*
 *	Fill in class i.d. by name
 */
	nchar = nCopyAnsiToWideChar(p, TEXT("STATIC"));
	p += nchar;

/*
 *	Copy the text of the item
 */
	nchar = nCopyAnsiToWideChar(p, GOAHEAD_COPYRIGHT);
	p += nchar;

/*
 *	Advance pointer over nExtraStuff WORD
 */
	*p++ = 0;  
/*
 *	Make sure the next item starts on a DWORD boundary
 */
	p = lpwAlign(p);

/*
 *	Add third item ("Version:")
 */
	lStyle = SS_RIGHT | WS_VISIBLE | WS_CHILD | WS_TABSTOP;
	*p++ = LOWORD(lStyle);
	*p++ = HIWORD(lStyle);
	*p++ = 0;          
	*p++ = 0;
	*p++ = 28;
	*p++ = 50;
	*p++ = 70;
	*p++ = 10;
	*p++ = 1;
	nchar = nCopyAnsiToWideChar(p, T("STATIC"));
	p += nchar;
	nchar = nCopyAnsiToWideChar(p, T("Version:"));
	p += nchar;
	*p++ = 0;

/*
 *	Add fourth Item (IDC_VERSION)
 */
	p = lpwAlign(p);
	lStyle = SS_LEFT | WS_VISIBLE | WS_CHILD | WS_TABSTOP;
	*p++ = LOWORD(lStyle);
	*p++ = HIWORD(lStyle);
	*p++ = 0;          
	*p++ = 0;
	*p++ = 102;
	*p++ = 50;
	*p++ = 70;
	*p++ = 10;
	*p++ = IDC_VERSION;
	nchar = nCopyAnsiToWideChar(p, T("STATIC"));
	p += nchar;
	nchar = nCopyAnsiToWideChar(p, T("version"));
	p += nchar;
	*p++ = 0;
/*
 *	Add fifth item ("Build Date:")
 */
	p = lpwAlign(p);
	lStyle = SS_RIGHT | WS_VISIBLE | WS_CHILD | WS_TABSTOP;
	*p++ = LOWORD(lStyle);
	*p++ = HIWORD(lStyle);
	*p++ = 0;          
	*p++ = 0;
	*p++ = 28;
	*p++ = 65;
	*p++ = 70;
	*p++ = 10;
	*p++ = 1;
	nchar = nCopyAnsiToWideChar(p, T("STATIC"));
	p += nchar;
	nchar = nCopyAnsiToWideChar(p, T("Build Date:"));
	p += nchar;
	*p++ = 0;
/*
 *	Add sixth item (IDC_BUILDDATE)
 */
	p = lpwAlign(p);
	lStyle = SS_LEFT | WS_VISIBLE | WS_CHILD | WS_TABSTOP;
	*p++ = LOWORD(lStyle);
	*p++ = HIWORD(lStyle);
	*p++ = 0;          
	*p++ = 0;
	*p++ = 102;
	*p++ = 65;
	*p++ = 70;
	*p++ = 10;
	*p++ = IDC_BUILDDATE;
	nchar = nCopyAnsiToWideChar(p, T("STATIC"));
	p += nchar;
	nchar = nCopyAnsiToWideChar(p, T("Build Date"));
	p += nchar;
	*p++ = 0;
/*
 *	Add seventh item (IDOK)
 */
	p = lpwAlign(p);
	lStyle = BS_PUSHBUTTON | WS_VISIBLE | WS_CHILD | WS_TABSTOP;
	*p++ = LOWORD(lStyle);
	*p++ = HIWORD(lStyle);
	*p++ = 0;          
	*p++ = 0;
	*p++ = 80;
	*p++ = 80;
	*p++ = 40;
	*p++ = 10;
	*p++ = IDOK;
	nchar = nCopyAnsiToWideChar(p, T("BUTTON"));
	p += nchar;
	nchar = nCopyAnsiToWideChar(p, T("OK"));
	p += nchar;
	*p++ = 0;

	hwndReturn = CreateDialogIndirect(hInstance, 
		(LPDLGTEMPLATE) pdlgtemplate, hwnd, (DLGPROC) websAboutProc);

	LocalFree(LocalHandle(pdlgtemplate));

	return 0;
}

/******************************************************************************/
/*
 *	Test Javascript binding for ASP. This will be invoked when "aspTest" is
 *	embedded in an ASP page. See web/asp.asp for usage. Set browser to 
 *	"localhost/asp.asp" to test.
 */

static int aspTest(int eid, webs_t wp, int argc, char_t **argv)
{
	char_t	*name, *address;

	if (ejArgs(argc, argv, T("%s %s"), &name, &address) < 2) {
		websError(wp, 400, T("Insufficient args\n"));
		return -1;
	}
	return websWrite(wp, T("Name: %s, Address %s"), name, address);
}

/******************************************************************************/
/*
 *	Test form for posted data (in-memory CGI). This will be called when the
 *	form in web/forms.asp is invoked. Set browser to "localhost/forms.asp" to test.
 */

static void formTest(webs_t wp, char_t *path, char_t *query)
{
	char_t	*name, *address;

	name = websGetVar(wp, T("name"), T("Joe Smith")); 
	address = websGetVar(wp, T("address"), T("1212 Milky Way Ave.")); 

	websHeader(wp);
	websWrite(wp, T("<body><h2>Name: %s, Address: %s</h2>\n"), name, address);
	websFooter(wp);
	websDone(wp, 200);
}

/******************************************************************************/
/*
 *	Home page handler
 */

static int websHomePageHandler(webs_t wp, char_t *urlPrefix, char_t *webDir,
							int arg, char_t *url, char_t *path, char_t *query)
{
/*
 *	If the empty or "/" URL is invoked, redirect default URLs to the home page
 */
	if (*url == '\0' || gstrcmp(url, T("/")) == 0) {
		websRedirect(wp, T("home.asp"));
		return 1;
	}
	return 0;
}

/******************************************************************************/
/*
 *	Default error handler.  The developer should insert code to handle
 *	error messages in the desired manner.
 */

void defaultErrorHandler(int etype, char_t *msg)
{
#if 0
	write(1, msg, gstrlen(msg));
#endif
}

/******************************************************************************/
/*
 *	Trace log. Customize this function to log trace output
 */

void defaultTraceHandler(int level, char_t *buf)
{
/*
 *	The following code would write all trace regardless of level
 *	to stdout.
 */
#if 0
	if (buf) {
		write(1, buf, gstrlen(buf));
	}
#endif
}

/******************************************************************************/
/*
 *	Returns a pointer to an allocated qualified unique temporary file name.
 *	This filename must eventually be deleted with bfree().
 */

char_t *websGetCgiCommName()
{
	char_t	*pname1, *pname2;

	pname1 = tempnam(NULL, T("cgi"));
	pname2 = bstrdup(B_L, pname1);
	free(pname1);
	return pname2;
}

/******************************************************************************/
/*
 *	Convert a table of strings into a single block of memory.
 *	The input table consists of an array of null-terminated strings,
 *	terminated in a null pointer.
 *	Returns the address of a block of memory allocated using the balloc() 
 *	function.  The returned pointer must be deleted using bfree().
 *	Returns NULL on error.
 */

static unsigned char *tableToBlock(char **table)
{
    unsigned char	*pBlock;		/*  Allocated block */
    char			*pEntry;		/*  Pointer into block      */
    size_t			sizeBlock;		/*  Size of table           */
    int				index;			/*  Index into string table */

    a_assert(table);

/*  
 *	Calculate the size of the data block.  Allow for final null byte. 
 */
    sizeBlock = 1;                    
    for (index = 0; table[index]; index++) {
        sizeBlock += strlen(table[index]) + 1;
	}

/*
 *	Allocate the data block and fill it with the strings                   
 */
    pBlock = balloc(B_L, sizeBlock);

	if (pBlock != NULL) {
		pEntry = (char *) pBlock;

        for (index = 0; table[index]; index++) {
			strcpy(pEntry, table[index]);
			pEntry += strlen(pEntry) + 1;
		}

/*		
 *		Terminate the data block with an extra null string                
 */
		*pEntry = '\0';              
	}

	return pBlock;
}


/******************************************************************************/
/*
 *	Create a temporary stdout file and launch the CGI process.
 *	Returns a handle to the spawned CGI process.
 */

int websLaunchCgiProc(char_t *cgiPath, char_t **argp, char_t **envp, 
					  char_t *stdIn, char_t *stdOut)
{
    STARTUPINFO			newinfo;	
	SECURITY_ATTRIBUTES security;
    PROCESS_INFORMATION	procinfo;		/*  Information about created proc   */
    DWORD				dwCreateFlags;
    char_t				*cmdLine;
	char_t				**pArgs;
	BOOL				bReturn;
	int					i, nLen;
	unsigned char		*pEnvData;

/*
 *	Replace directory delimiters with Windows-friendly delimiters
 */
	nLen = gstrlen(cgiPath);
	for (i = 0; i < nLen; i++) {
		if (cgiPath[i] == '/') {
			cgiPath[i] = '\\';
		}
	}
/*
 *	Calculate length of command line
 */
	nLen = 0;
	pArgs = argp;
	while (pArgs && *pArgs && **pArgs) {
		nLen += gstrlen(*pArgs) + 1;
		pArgs++;
	}

/*
 *	Construct command line
 */
	cmdLine = balloc(B_L, sizeof(char_t) * nLen);
	a_assert (cmdLine);
	gstrcpy(cmdLine, "");

	pArgs = argp;
	while (pArgs && *pArgs && **pArgs) {
		gstrcat(cmdLine, *pArgs);
		gstrcat(cmdLine, T(" "));
		pArgs++;
	}

 /*
 *	Create the process start-up information 
 */
	memset (&newinfo, 0, sizeof(newinfo));
    newinfo.cb          = sizeof(newinfo);
    newinfo.dwFlags     = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    newinfo.wShowWindow = SW_HIDE;
    newinfo.lpTitle     = NULL;

/*
 *	Create file handles for the spawned processes stdin and stdout files
 */
	security.nLength = sizeof(SECURITY_ATTRIBUTES);
	security.lpSecurityDescriptor = NULL;
	security.bInheritHandle = TRUE;

/*
 *	Stdin file should already exist.
 */
	newinfo.hStdInput = CreateFile(stdIn, GENERIC_READ,
		FILE_SHARE_READ, &security, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

/*
 *	Stdout file is created and file pointer is reset to start.
 */
	newinfo.hStdOutput = CreateFile(stdOut, GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ + FILE_SHARE_WRITE, &security, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, NULL);
	SetFilePointer (newinfo.hStdOutput, 0, NULL, FILE_END);

/*
 *	Stderr file is set to Stdout.
 */
	newinfo.hStdError =	newinfo.hStdOutput;

	dwCreateFlags = CREATE_NEW_CONSOLE;
	pEnvData = tableToBlock(envp);

/*
 *	CreateProcess returns errors sometimes, even when the process was    
 *	started correctly.  The cause is not evident.  For now: we detect    
 *	an error by checking the value of procinfo.hProcess after the call.  
 */
    procinfo.hProcess = NULL;
    bReturn = CreateProcess(
        NULL,				/*  Name of executable module        */
        cmdLine,			/*  Command line string              */
        NULL,				/*  Process security attributes      */
        NULL,				/*  Thread security attributes       */
        TRUE,				/*  Handle inheritance flag          */
        dwCreateFlags,		/*  Creation flags                   */
        pEnvData,			/*  New environment block            */
        NULL,				/*  Current directory name           */
        &newinfo,			/*  STARTUPINFO                      */
        &procinfo);			/*  PROCESS_INFORMATION              */

	if (procinfo.hThread != NULL)  {
		CloseHandle(procinfo.hThread);
	}

	if (newinfo.hStdInput) {
		CloseHandle(newinfo.hStdInput);
	}

	if (newinfo.hStdOutput) {
		CloseHandle(newinfo.hStdOutput);
	}

	bfree(B_L, pEnvData);
	bfree(B_L, cmdLine);

	if (bReturn == 0) {
		return -1;
	} else {
		return (int) procinfo.hProcess;
	}
}

/******************************************************************************/
/*
 *	Check the CGI process.  Return 0 if it does not exist; non 0 if it does.
 */

int websCheckCgiProc(int handle)
{
	int		nReturn;
	DWORD	exitCode;

	nReturn = GetExitCodeProcess((HANDLE)handle, &exitCode);
/*
 *	We must close process handle to free up the window resource, but only
 *  when we're done with it.
 */
	if ((nReturn == 0) || (exitCode != STILL_ACTIVE)) {
		CloseHandle((HANDLE)handle);
		return 0;
	}

	return 1;
}

/******************************************************************************/

#ifdef B_STATS
static void memLeaks() 
{
	int		fd;

	if ((fd = gopen(T("leak.txt"), O_CREAT | O_TRUNC | O_WRONLY)) >= 0) {
		bstats(fd, printMemStats);
		close(fd);
	}
}

/******************************************************************************/
/*
 *	Print memory usage / leaks  
 */

static void printMemStats(int handle, char_t *fmt, ...)
{
	va_list		args;
	char_t		buf[256];

	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	va_end(args);
	write(handle, buf, strlen(buf));
}
#endif

/******************************************************************************/
/*
 *	Center window on screen
 */

#define RCWIDTH(rc) ((rc).right - (rc).left)
#define RCHEIGHT(rc) ((rc).bottom - (rc).top)

static void centerWindowOnDisplay(HWND hwndCenter)
{
	int			xLeft, yTop, cxDisp, cyDisp;
	RECT		rcDlg;

	a_assert(IsWindow(hwndCenter));

/*
 *	Get Window Size
 */
	GetWindowRect(hwndCenter, &rcDlg);

/*
 *	Get monitor width and height
 */
	cxDisp = GetSystemMetrics(SM_CXFULLSCREEN);
	cyDisp = GetSystemMetrics(SM_CYFULLSCREEN);

/*
 *	Find dialog's upper left based on screen size
 */
	xLeft = cxDisp / 2 - RCWIDTH(rcDlg) / 2;
	yTop = cyDisp / 2 - RCHEIGHT(rcDlg) / 2;

/*
 *	If the dialog is outside the screen, move it inside
 */
	if (xLeft < 0) {
		xLeft = 0;
	} else if (xLeft + RCWIDTH(rcDlg) > cxDisp) {
		xLeft = cxDisp - RCWIDTH(rcDlg);
	}

	if (yTop < 0) {
		yTop = 0;
	} else if (yTop + RCHEIGHT(rcDlg) > cyDisp) {
		yTop = cyDisp - RCHEIGHT(rcDlg);
	}

/*
 *	Move the window
 */
	SetWindowPos(hwndCenter, HWND_TOP, xLeft, yTop, -1, -1,
		SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

/******************************************************************************/
