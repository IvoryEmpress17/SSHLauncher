#ifndef RESOURCE_H
#define RESOURCE_H

#define WIDTH 340
#define HEIGHT 292

#define IDD_MAIN_DIALOG        100

#define IDD_SELECT_SSH         300
#define IDC_COMBO_SSH_PATH     3001
#define IDC_CHK_REMEMBER_SSH   3002
#define IDC_BANNER_INSECURE    3003
#define IDC_BTN_ADD_CUSTOM     3004
#define IDC_BTN_DEL_CUSTOM     3005

#define IDC_EDIT_HOST          1001
#define IDC_EDIT_PORT          1002
#define IDC_EDIT_USER          1003
#define IDC_EDIT_TIMEOUT       1004
#define IDC_EDIT_EXTRA_ARGS    1005
#define IDC_EDIT_KEY_FILE      1006

#define IDC_RADIO_PASSWORD     1011
#define IDC_RADIO_KEY          1012

#define IDC_CHK_COMPRESS       1021
#define IDC_CHK_X11_FORWARD    1022
#define IDC_CHK_VERBOSE        1023
#define IDC_CHK_KEEPALIVE      1024

#define IDC_EDIT_LOCAL_FWD     1046
#define IDC_EDIT_REMOTE_FWD    1047
#define IDC_EDIT_DYNAMIC_FWD   1048
#define IDC_EDIT_PROXY_JUMP   1049

#define IDC_COMBO_ENCRYPTION   1031

#define IDC_BTN_CONNECT        1041
#define IDC_BTN_BROWSE_KEY     1042
#define IDC_BTN_CLEAR_LOG      1045

#define IDM_FILE_EXIT          2001
#define IDM_FILE_TOGGLE        2002
#define IDM_FILE_EXPORT_BAT    2004
#define IDM_FILE_OPEN_SSH_DIR  2005

#define IDM_TOOLS_PING         2401
#define IDM_TOOLS_SSH_TEST     2402
#define IDM_TOOLS_GEN_KEY      2403
#define IDM_TOOLS_COPY_PUB     2404
#define IDM_TOOLS_ADD_KEY      2405

#define IDM_HELP_ABOUT         2101
#define IDM_HELP_SSH           2102
#define IDM_HELP_GITHUB        2103

#define IDI_BAT                10001
#define IDI_GITHUB             10002
#define IDI_SSHHELP            10003

#define IDC_STATUS_BAR         1901

#endif
