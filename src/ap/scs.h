#ifndef SCS_H
#define SCS_H

struct hostapd_data;

#define SCS_MAX_CFG_CNT 2

struct scs_status_duple {
	u8 scs_id;
	u16 status;
};

struct scs_session_status {
	u8 scs_id;
	bool alive;
};

enum scs_req_type {
	SCS_REQ_TYPE_ADD,
	SCS_REQ_TYPE_REMOVE,
	SCS_REQ_TYPE_CHANGE,
};

#define SCS_REQ_SUCCESS				0
#define SCS_REQ_DECLINED			37
#define SCS_REQ_TCLAS_PROCESSING_TERMINATED	97

void hostapd_handle_scs(struct hostapd_data *hapd, const u8 *buf, size_t len);

#endif
