#ifndef WIFI_IWL_H
#define WIFI_IWL_H
/* Intel Wi-Fi 6 (iwlwifi So/Ty/Ma families): see wifi_iwl.c. */
void wifi_iwl_init(void);
int wifi_iwl_present(void);
void wifi_iwl_report(void);   /* lines for `devices` */
#endif
