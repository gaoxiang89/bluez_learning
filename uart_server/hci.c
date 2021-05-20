#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include "log.h"


int le_set_advertising_parameters(uint16_t	min_interval, uint16_t	max_interval, uint8_t chan_map)
{
	struct hci_request rq;
	le_set_advertising_parameters_cp adv_params_cp;
	le_set_advertise_enable_cp advertise_cp;
	uint8_t status;
	int dd, ret = 0;

	/*
	 * 打开hci0
	 */
	dd = hci_open_dev(0);
	
	u_tm_log("[%s:%d] hci0 dd = %d !\n", __FUNCTION__, __LINE__, dd);
	
	if(dd < 0) {
		u_tm_log("Error: [%s:%d] hci0 open failed !\n", __FUNCTION__, __LINE__);
		return -1;
	}

	if(min_interval > max_interval) {
		ret = -1;
		goto done;
	}
	
	memset(&adv_params_cp, 0, sizeof(adv_params_cp));
	adv_params_cp.min_interval = htobs(min_interval);
	adv_params_cp.max_interval = htobs(max_interval);
	adv_params_cp.chan_map = chan_map;

	memset(&rq, 0, sizeof(rq));
	rq.ogf = OGF_LE_CTL;
	rq.ocf = OCF_LE_SET_ADVERTISING_PARAMETERS;
	rq.cparam = &adv_params_cp;
	rq.clen = LE_SET_ADVERTISING_PARAMETERS_CP_SIZE;
	rq.rparam = &status;
	rq.rlen = 1;

	ret = hci_send_req(dd, &rq, 1000);
	u_tm_log("[%s:%d] ADVERTISING_PARAMETERS status = %d !\n", __FUNCTION__, __LINE__, status);
	if(ret < 0) {
		u_tm_log("[%s:%d] ADVERTISING_PARAMETERS status = %d !\n", __FUNCTION__, __LINE__, status);
		goto done;
	}

done:
	hci_close_dev(dd);
	return ret;
}

