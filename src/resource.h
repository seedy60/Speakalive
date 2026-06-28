/* resource.h - control, command and resource identifiers for Speakalive */
#ifndef SPEAKALIVE_RESOURCE_H
#define SPEAKALIVE_RESOURCE_H

/* ---- Child control IDs ---- */
#define IDC_TAB         1001
#define IDC_VOICELBL    1002
#define IDC_VOICE       1003
#define IDC_TEXTLBL     1004
#define IDC_TEXT        1005
#define IDC_RATELBL     1006
#define IDC_RATE        1007
#define IDC_RATEVAL     1008
#define IDC_PITCHLBL    1009
#define IDC_PITCH       1010
#define IDC_PITCHVAL    1011
#define IDC_VOLLBL      1012
#define IDC_VOLUME      1013
#define IDC_VOLVAL      1014
#define IDC_RESET       1015
#define IDC_SPEAK       1016
#define IDC_PAUSE       1017
#define IDC_STOP        1018
#define IDC_STATUS      1019

/* ---- Menu command IDs ---- */
#define IDM_SAVE        2001
#define IDM_EXIT        2002
#define IDM_SPEAK       2003
#define IDM_PAUSE       2004
#define IDM_STOP        2005
#define IDM_XML         2006
#define IDM_RESET       2007
#define IDM_ABOUT       2008
#define IDM_SAVETEXT    2009
#define IDM_DARKMODE    2010
#define IDM_FOLLOWOS    2011
#define IDM_HIGHLIGHT   2012
#define IDM_NEXTENGINE  2013
#define IDM_PREVENGINE  2014
#define IDM_WEBPAGE     2015
#define IDM_STEREO      2016
#define IDM_RENAMEVOICE 2017
#define IDM_VOICEEXPORT 2018
#define IDM_VOICEIMPORT 2019

/* ---- Resources ---- */
#define IDA_ACCEL       3001
#define IDI_MAIN        4001
#define IDR_MENU        5001

/* ---- "Open Web Page" dialog ---- */
#define IDD_URL         6001
#define IDC_URLLABEL    6002
#define IDC_URLEDIT     6003

/* ---- "Saving Audio" progress dialog ---- */
#define IDD_PROGRESS    6004
#define IDC_PROGRESSLBL 6005
#define IDC_PROGRESSBAR 6006

/* ---- "Rename Voice" dialog ---- */
#define IDD_RENAME      6007
#define IDC_RENAMELBL   6008
#define IDC_RENAMEEDIT  6009

#endif /* SPEAKALIVE_RESOURCE_H */
