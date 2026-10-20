#ifndef RESOURCE_H
#define RESOURCE_H

#ifndef IDC_STATIC
#define IDC_STATIC (-1)
#endif

#define IDI_APP_ICON        101

#define IDD_SETUP_DIALOG    201
#define IDC_RADIO_TELEX     202
#define IDC_RADIO_VNI       203
#define IDC_CHECK_MODERN    204
#define IDC_CHECK_SPELL     205
#define IDC_CHECK_STARTUP   206
#define IDC_BTN_OK          207
#define IDC_BTN_CANCEL      208
#define IDC_CHECK_SUGGEST  209
#define IDC_BTN_DICT_UPDATE 210 /* PUSHBUTTON (was EDITTEXT model combo) */
/* Labels need individual IDs: every visible string is set at runtime
 * (UTF-8 -> UTF-16) so the dialog never depends on how windres decodes
 * non-ASCII resource text. */
#define IDC_GROUP_TYPE      212
#define IDC_GROUP_OPTS      213
#define IDC_GROUP_DICT      214
#define IDC_LBL_DICT_VER    215

#define ID_TRAY_SETTINGS    302
#define ID_TRAY_MODE_TELEX  303
#define ID_TRAY_MODE_VNI    304
#define ID_TRAY_SPELLCHECK  305
#define ID_TRAY_STARTUP     306
#define ID_TRAY_EXIT        307
#define ID_TRAY_MODERN      308
#define ID_TRAY_UPDATE      309
#define ID_TRAY_STARTUP_ADMIN 312
#define ID_TRAY_KEYBOARDS   313

#define IDC_BTN_DATA        217
#define IDC_BTN_KEYBOARD    253
#define IDC_LBL_DICT_STATUS 233
#define IDC_CHECK_DICT_VI     234
#define IDC_CHECK_DICT_TSQL   235
#define IDC_CHECK_DICT_C      236
#define IDC_CHECK_DICT_CSHARP 237
#define IDC_CHECK_DICT_EN     238

#define IDD_DATA_DIALOG     220
#define IDC_DATA_MACRO      221
#define IDC_DATA_EMOJI      222
#define IDC_DATA_LIST       223
#define IDC_DATA_KEY        224
#define IDC_DATA_VALUE      225
#define IDC_LBL_KEY         226
#define IDC_LBL_VALUE       227
#define IDC_DATA_SAVE       228
#define IDC_DATA_DELETE     229
#define IDC_DATA_FOLDER     230
#define IDC_DATA_CLOSE      231
#define IDC_DATA_NOTE       232

/* Keyboard (Win+Space) manager: current list + "add available" picker. */
#define IDD_KEYBOARD_DIALOG 240
#define IDD_KBD_ADD_DIALOG  260
#define IDC_KBD_LIST        241
#define IDC_KBD_UP          242
#define IDC_KBD_DOWN        243
#define IDC_KBD_REMOVE      244
#define IDC_KBD_ADD         245
#define IDC_KBD_DEFAULT     246
#define IDC_KBD_CLOSE       247
#define IDC_KBD_STATUS      248
#define IDC_KBD_LANG        249
#define IDC_KBD_AVAIL       250
#define IDC_KBD_AVAIL_ADD   251
#define IDC_KBD_LBL_CURRENT 254

#endif /* RESOURCE_H */
