#include "utils/includes.h"

#include "utils/common.h"
#include "common/ieee802_11_defs.h"
#include "common/ieee802_11_common.h"
#include "hostapd.h"
#include "ieee802_11.h"
#include "sta_info.h"
#include "ap_drv_ops.h"
#include "scs.h"

#define SCS_DIRECTION_UPLINK 0

static bool hostapd_find_scs_session(struct sta_info *sta, u8 scsid,
				     u8 *session_idx)
{
	u8 idx;

	for (idx = 0; idx < SCS_MAX_CFG_CNT; idx++) {
		if (sta->scs_session[idx].scs_id == scsid) {
			*session_idx = idx;
			return sta->scs_session[idx].alive;
		}
	}

	return false;
}


static int hostapd_find_available_scs_session(struct sta_info *sta)
{
	u8 idx;

	for (idx = 0; idx < SCS_MAX_CFG_CNT; idx++) {
		if (!sta->scs_session[idx].alive)
			return idx;
	}

	return -1;
}


static bool hostapd_parse_qos_char_element(const struct element *elem,
					   struct hostapd_scs_desc_info *info)
{
	u32 control_info;

	if (elem->datalen < 5 ||
	    elem->data[0] != WLAN_EID_EXT_QOS_CHARACTERISTICS)
		return false;

	info->qos_ie_len = elem->datalen + 2;
	if (info->qos_ie_len > sizeof(info->qos_ie))
		return false;

	control_info = WPA_GET_LE32(&elem->data[1]);
	info->dir = control_info & 0x3;
	if (info->dir != SCS_DIRECTION_UPLINK)
		return false;

	os_memcpy(info->qos_ie, elem, info->qos_ie_len);

	return true;
}


static u16 hostapd_process_scs_descriptor(struct hostapd_data *hapd,
					  struct sta_info *sta,
					  const u8 *payload,
					  u8 scs_desc_len,
					  struct hostapd_scs_desc_info *info)
{
	bool scs_avail, qos_char_elem_avail = false;
	const struct element *elem;
	u8 session_idx = 0;
	int ret;

	if (scs_desc_len < 2)
		goto decline;

	scs_avail = hostapd_find_scs_session(sta, info->id, &session_idx);

	switch (info->req_type) {
	case SCS_REQ_TYPE_ADD:
	case SCS_REQ_TYPE_CHANGE:
		if ((info->req_type == SCS_REQ_TYPE_ADD && scs_avail) ||
		    (info->req_type == SCS_REQ_TYPE_CHANGE && !scs_avail))
			goto decline;

		if (info->req_type == SCS_REQ_TYPE_ADD) {
			int idx = hostapd_find_available_scs_session(sta);

			if (idx < 0) {
				wpa_printf(MSG_ERROR, "%s: Out of SCS resources",
					   __func__);
				goto decline;
			}
			session_idx = idx;
		}

		for_each_element(elem, payload + 2, scs_desc_len - 2) {
			if (elem->id == WLAN_EID_EXTENSION)
				qos_char_elem_avail =
					hostapd_parse_qos_char_element(elem, info);
		}

		if (!qos_char_elem_avail) {
			wpa_printf(MSG_ERROR,
				   "%s: missing or unsupported QoS Characteristics element",
				   __func__);
			goto decline;
		}
		break;
	case SCS_REQ_TYPE_REMOVE:
		if (!scs_avail)
			goto decline;
		break;
	default:
		goto decline;
	}

	ret = hostapd_drv_set_scs(hapd, info);
	if (ret)
		goto decline;

	sta->scs_session[session_idx].scs_id = info->id;
	sta->scs_session[session_idx].alive =
		info->req_type != SCS_REQ_TYPE_REMOVE;

	return info->req_type == SCS_REQ_TYPE_REMOVE ?
		SCS_REQ_TCLAS_PROCESSING_TERMINATED : SCS_REQ_SUCCESS;

decline:
	wpa_printf(MSG_ERROR, "%s: decline request type %u",
		   __func__, info->req_type);
	return SCS_REQ_DECLINED;
}


static void send_scs_response(struct hostapd_data *hapd,
			      struct scs_status_duple *scs_status,
			      const u8 *da, u8 dialog_token, u8 count)
{
	struct wpabuf *buf;
	size_t len;
	u8 i;

	if (!count)
		return;

	len = 4 + count * sizeof(struct scs_status_duple);
	buf = wpabuf_alloc(len);
	if (!buf)
		return;

	wpabuf_put_u8(buf, WLAN_ACTION_ROBUST_AV_STREAMING);
	wpabuf_put_u8(buf, ROBUST_AV_SCS_RESP);
	wpabuf_put_u8(buf, dialog_token);
	wpabuf_put_u8(buf, count);

	for (i = 0; i < count && i < SCS_MAX_CFG_CNT; i++) {
		wpabuf_put_u8(buf, scs_status[i].scs_id);
		wpabuf_put_le16(buf, scs_status[i].status);
	}

	hostapd_drv_send_action(hapd, hapd->iface->freq, 0, da,
				wpabuf_head(buf), wpabuf_len(buf));
	wpabuf_free(buf);
}


static void hostapd_handle_scs_req(struct hostapd_data *hapd,
				   const u8 *buf, size_t len)
{
	const struct ieee80211_mgmt *mgmt = (const struct ieee80211_mgmt *) buf;
	struct scs_status_duple scs_status_list[SCS_MAX_CFG_CNT];
	const struct element *elem;
	struct sta_info *sta;
	const u8 *pos, *end;
	u8 token, index = 0;

	sta = ap_get_sta(hapd, mgmt->sa);
	if (!sta) {
		wpa_printf(MSG_ERROR, "Station " MACSTR
			   " not found for SCS Request frame",
			   MAC2STR(mgmt->sa));
		return;
	}

	token = buf[IEEE80211_HDRLEN + 2];
	pos = buf + IEEE80211_HDRLEN + 3;
	end = buf + len;

	for_each_element(elem, pos, end - pos) {
		struct hostapd_scs_desc_info info;

		if (index >= SCS_MAX_CFG_CNT)
			break;

		if (elem->id != WLAN_EID_SCS_DESCRIPTOR || elem->datalen < 2) {
			wpa_printf(MSG_ERROR, "%s: invalid SCS descriptor",
				   __func__);
			break;
		}

		os_memset(&info, 0, sizeof(info));
		info.id = elem->data[0];
		info.req_type = elem->data[1];
		os_memcpy(info.peer_addr, mgmt->sa, ETH_ALEN);

		if (!info.id) {
			wpa_printf(MSG_ERROR, "%s: SCSID 0 is invalid",
				   __func__);
			break;
		}

		scs_status_list[index].scs_id = info.id;
		scs_status_list[index].status =
			hostapd_process_scs_descriptor(hapd, sta, elem->data,
						       elem->datalen, &info);
		index++;
	}

	send_scs_response(hapd, scs_status_list, mgmt->sa, token, index);
}


void hostapd_handle_scs(struct hostapd_data *hapd, const u8 *buf, size_t len)
{
	u8 action;

	if (len < IEEE80211_HDRLEN + 3) {
		wpa_printf(MSG_ERROR, "%s: SCS frame too short len=%lu",
			   __func__, (unsigned long) len);
		return;
	}

	action = buf[IEEE80211_HDRLEN + 1];
	if (action == ROBUST_AV_SCS_REQ)
		hostapd_handle_scs_req(hapd, buf, len);
}
