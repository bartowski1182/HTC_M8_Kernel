/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "../wifi.h"
#include "../base.h"
#include "reg.h"
#include "def.h"
#include "phy.h"
#include "dm.h"
#include "fw.h"

struct dig_t digtable;
static const u32 edca_setting_dl[PEER_MAX] = {
	0xa44f,		
	0x5ea44f,	
	0x5ea44f,	
	0xa630,		
	0xa44f,		
	0xa630,		
	0xa630,		
	0xa42b,		
};

static const u32 edca_setting_dl_gmode[PEER_MAX] = {
	0x4322,		
	0xa44f,		
	0x5ea44f,	
	0xa42b,		
	0x5e4322,	
	0x4322,		
	0xa430,		
	0x5ea44f,	
};

static const u32 edca_setting_ul[PEER_MAX] = {
	0x5e4322,	
	0xa44f,		
	0x5ea44f,	
	0x5ea322,	
	0x5ea422,	
	0x5ea322,	
	0x3ea44f,	
	0x5ea44f,	
};

static void _rtl92s_dm_check_edca_turbo(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));

	static u64 last_txok_cnt;
	static u64 last_rxok_cnt;
	u64 cur_txok_cnt = 0;
	u64 cur_rxok_cnt = 0;

	u32 edca_be_ul = edca_setting_ul[mac->vendor];
	u32 edca_be_dl = edca_setting_dl[mac->vendor];
	u32 edca_gmode = edca_setting_dl_gmode[mac->vendor];

	if (mac->link_state != MAC80211_LINKED) {
		rtlpriv->dm.current_turbo_edca = false;
		goto dm_checkedcaturbo_exit;
	}

	if ((!rtlpriv->dm.is_any_nonbepkts) &&
	    (!rtlpriv->dm.disable_framebursting)) {
		cur_txok_cnt = rtlpriv->stats.txbytesunicast - last_txok_cnt;
		cur_rxok_cnt = rtlpriv->stats.rxbytesunicast - last_rxok_cnt;

		if (rtlpriv->phy.rf_type == RF_1T2R) {
			if (cur_txok_cnt > 4 * cur_rxok_cnt) {
				
				if (rtlpriv->dm.is_cur_rdlstate ||
					!rtlpriv->dm.current_turbo_edca) {
					rtl_write_dword(rtlpriv, EDCAPARA_BE,
							edca_be_ul);
					rtlpriv->dm.is_cur_rdlstate = false;
				}
			} else {
				if (!rtlpriv->dm.is_cur_rdlstate ||
					!rtlpriv->dm.current_turbo_edca) {
					if (mac->mode == WIRELESS_MODE_G ||
					    mac->mode == WIRELESS_MODE_B)
						rtl_write_dword(rtlpriv,
								EDCAPARA_BE,
								edca_gmode);
					else
						rtl_write_dword(rtlpriv,
								EDCAPARA_BE,
								edca_be_dl);
					rtlpriv->dm.is_cur_rdlstate = true;
				}
			}
			rtlpriv->dm.current_turbo_edca = true;
		} else {
			if (cur_rxok_cnt > 4 * cur_txok_cnt) {
				if (!rtlpriv->dm.is_cur_rdlstate ||
					!rtlpriv->dm.current_turbo_edca) {
					if (mac->mode == WIRELESS_MODE_G ||
					    mac->mode == WIRELESS_MODE_B)
						rtl_write_dword(rtlpriv,
								EDCAPARA_BE,
								edca_gmode);
					else
						rtl_write_dword(rtlpriv,
								EDCAPARA_BE,
								edca_be_dl);
					rtlpriv->dm.is_cur_rdlstate = true;
				}
			} else {
				if (rtlpriv->dm.is_cur_rdlstate ||
					!rtlpriv->dm.current_turbo_edca) {
					rtl_write_dword(rtlpriv, EDCAPARA_BE,
							edca_be_ul);
					rtlpriv->dm.is_cur_rdlstate = false;
				}
			}
			rtlpriv->dm.current_turbo_edca = true;
		}
	} else {
		if (rtlpriv->dm.current_turbo_edca) {
			u8 tmp = AC0_BE;
			rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_AC_PARAM,
						      (u8 *)(&tmp));
			rtlpriv->dm.current_turbo_edca = false;
		}
	}

dm_checkedcaturbo_exit:
	rtlpriv->dm.is_any_nonbepkts = false;
	last_txok_cnt = rtlpriv->stats.txbytesunicast;
	last_rxok_cnt = rtlpriv->stats.rxbytesunicast;
}

static void _rtl92s_dm_txpowertracking_callback_thermalmeter(
					struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_efuse *rtlefuse = rtl_efuse(rtl_priv(hw));
	u8 thermalvalue = 0;

	rtlpriv->dm.txpower_trackinginit = true;

	thermalvalue = (u8)rtl_get_rfreg(hw, RF90_PATH_A, RF_T_METER, 0x1f);

	RT_TRACE(rtlpriv, COMP_POWER_TRACKING, DBG_LOUD,
		 "Readback Thermal Meter = 0x%x pre thermal meter 0x%x eeprom_thermal meter 0x%x\n",
		 thermalvalue,
		 rtlpriv->dm.thermalvalue, rtlefuse->eeprom_thermalmeter);

	if (thermalvalue) {
		rtlpriv->dm.thermalvalue = thermalvalue;
		rtl92s_phy_set_fw_cmd(hw, FW_CMD_TXPWR_TRACK_THERMAL);
	}

	rtlpriv->dm.txpowercount = 0;
}

static void _rtl92s_dm_check_txpowertracking_thermalmeter(
					struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	static u8 tm_trigger;
	u8 tx_power_checkcnt = 5;

	
	if (rtlphy->rf_type == RF_2T2R)
		return;

	if (!rtlpriv->dm.txpower_tracking)
		return;

	if (rtlpriv->dm.txpowercount <= tx_power_checkcnt) {
		rtlpriv->dm.txpowercount++;
		return;
	}

	if (!tm_trigger) {
		rtl_set_rfreg(hw, RF90_PATH_A, RF_T_METER,
			      RFREG_OFFSET_MASK, 0x60);
		tm_trigger = 1;
	} else {
		_rtl92s_dm_txpowertracking_callback_thermalmeter(hw);
		tm_trigger = 0;
	}
}

static void _rtl92s_dm_refresh_rateadaptive_mask(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rate_adaptive *ra = &(rtlpriv->ra);

	u32 low_rssi_thresh = 0;
	u32 middle_rssi_thresh = 0;
	u32 high_rssi_thresh = 0;
	struct ieee80211_sta *sta = NULL;

	if (is_hal_stop(rtlhal))
		return;

	if (!rtlpriv->dm.useramask)
		return;

	if (!rtlpriv->dm.inform_fw_driverctrldm) {
		rtl92s_phy_set_fw_cmd(hw, FW_CMD_CTRL_DM_BY_DRIVER);
		rtlpriv->dm.inform_fw_driverctrldm = true;
	}

	rcu_read_lock();
	if (mac->opmode == NL80211_IFTYPE_STATION)
		sta = get_sta(hw, mac->vif, mac->bssid);
	if ((mac->link_state == MAC80211_LINKED) &&
	    (mac->opmode == NL80211_IFTYPE_STATION)) {
		switch (ra->pre_ratr_state) {
		case DM_RATR_STA_HIGH:
			high_rssi_thresh = 40;
			middle_rssi_thresh = 30;
			low_rssi_thresh = 20;
			break;
		case DM_RATR_STA_MIDDLE:
			high_rssi_thresh = 44;
			middle_rssi_thresh = 30;
			low_rssi_thresh = 20;
			break;
		case DM_RATR_STA_LOW:
			high_rssi_thresh = 44;
			middle_rssi_thresh = 34;
			low_rssi_thresh = 20;
			break;
		case DM_RATR_STA_ULTRALOW:
			high_rssi_thresh = 44;
			middle_rssi_thresh = 34;
			low_rssi_thresh = 24;
			break;
		default:
			high_rssi_thresh = 44;
			middle_rssi_thresh = 34;
			low_rssi_thresh = 24;
			break;
		}

		if (rtlpriv->dm.undecorated_smoothed_pwdb >
		    (long)high_rssi_thresh) {
			ra->ratr_state = DM_RATR_STA_HIGH;
		} else if (rtlpriv->dm.undecorated_smoothed_pwdb >
			   (long)middle_rssi_thresh) {
			ra->ratr_state = DM_RATR_STA_LOW;
		} else if (rtlpriv->dm.undecorated_smoothed_pwdb >
			   (long)low_rssi_thresh) {
			ra->ratr_state = DM_RATR_STA_LOW;
		} else {
			ra->ratr_state = DM_RATR_STA_ULTRALOW;
		}

		if (ra->pre_ratr_state != ra->ratr_state) {
			RT_TRACE(rtlpriv, COMP_RATE, DBG_LOUD,
				 "RSSI = %ld RSSI_LEVEL = %d PreState = %d, CurState = %d\n",
				 rtlpriv->dm.undecorated_smoothed_pwdb,
				 ra->ratr_state,
				 ra->pre_ratr_state, ra->ratr_state);

			rtlpriv->cfg->ops->update_rate_tbl(hw, sta,
							   ra->ratr_state);
			ra->pre_ratr_state = ra->ratr_state;
		}
	}
	rcu_read_unlock();
}

static void _rtl92s_dm_switch_baseband_mrc(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	bool current_mrc;
	bool enable_mrc = true;
	long tmpentry_maxpwdb = 0;
	u8 rssi_a = 0;
	u8 rssi_b = 0;

	if (is_hal_stop(rtlhal))
		return;

	if ((rtlphy->rf_type == RF_1T1R) || (rtlphy->rf_type == RF_2T2R))
		return;

	rtlpriv->cfg->ops->get_hw_reg(hw, HW_VAR_MRC, (u8 *)(&current_mrc));

	if (mac->link_state >= MAC80211_LINKED) {
		if (rtlpriv->dm.undecorated_smoothed_pwdb > tmpentry_maxpwdb) {
			rssi_a = rtlpriv->stats.rx_rssi_percentage[RF90_PATH_A];
			rssi_b = rtlpriv->stats.rx_rssi_percentage[RF90_PATH_B];
		}
	}

	
	if (mac->mode != WIRELESS_MODE_B) {
		if ((rssi_a == 0) && (rssi_b == 0)) {
			enable_mrc = true;
		} else if (rssi_b > 30) {
			
			enable_mrc = true;
		} else if (rssi_b < 5) {
			
			enable_mrc = false;
		
		} else if (rssi_a > 15 && (rssi_a >= rssi_b)) {
			if ((rssi_a - rssi_b) > 15)
				
				enable_mrc = false;
			else if ((rssi_a - rssi_b) < 10)
				
				enable_mrc = true;
			else
				enable_mrc = current_mrc;
		} else {
			
			enable_mrc = true;
		}
	}

	
	if (enable_mrc != current_mrc)
		rtlpriv->cfg->ops->set_hw_reg(hw, HW_VAR_MRC,
					      (u8 *)&enable_mrc);

}

void rtl92s_dm_init_edca_turbo(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.current_turbo_edca = false;
	rtlpriv->dm.is_any_nonbepkts = false;
	rtlpriv->dm.is_cur_rdlstate = false;
}

static void _rtl92s_dm_init_rate_adaptive_mask(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rate_adaptive *ra = &(rtlpriv->ra);

	ra->ratr_state = DM_RATR_STA_MAX;
	ra->pre_ratr_state = DM_RATR_STA_MAX;

	if (rtlpriv->dm.dm_type == DM_TYPE_BYDRIVER)
		rtlpriv->dm.useramask = true;
	else
		rtlpriv->dm.useramask = false;

	rtlpriv->dm.useramask = false;
	rtlpriv->dm.inform_fw_driverctrldm = false;
}

static void _rtl92s_dm_init_txpowertracking_thermalmeter(
				struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.txpower_tracking = true;
	rtlpriv->dm.txpowercount = 0;
	rtlpriv->dm.txpower_trackinginit = false;
}

static void _rtl92s_dm_false_alarm_counter_statistics(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct false_alarm_statistics *falsealm_cnt = &(rtlpriv->falsealm_cnt);
	u32 ret_value;

	ret_value = rtl_get_bbreg(hw, ROFDM_PHYCOUNTER1, MASKDWORD);
	falsealm_cnt->cnt_parity_fail = ((ret_value & 0xffff0000) >> 16);

	ret_value = rtl_get_bbreg(hw, ROFDM_PHYCOUNTER2, MASKDWORD);
	falsealm_cnt->cnt_rate_illegal = (ret_value & 0xffff);
	falsealm_cnt->cnt_crc8_fail = ((ret_value & 0xffff0000) >> 16);
	ret_value = rtl_get_bbreg(hw, ROFDM_PHYCOUNTER3, MASKDWORD);
	falsealm_cnt->cnt_mcs_fail = (ret_value & 0xffff);

	falsealm_cnt->cnt_ofdm_fail = falsealm_cnt->cnt_parity_fail +
		falsealm_cnt->cnt_rate_illegal + falsealm_cnt->cnt_crc8_fail +
		falsealm_cnt->cnt_mcs_fail;

	
	ret_value = rtl_get_bbreg(hw, 0xc64, MASKDWORD);
	falsealm_cnt->cnt_cck_fail = (ret_value & 0xffff);
	falsealm_cnt->cnt_all =	falsealm_cnt->cnt_ofdm_fail +
		falsealm_cnt->cnt_cck_fail;
}

static void rtl92s_backoff_enable_flag(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct false_alarm_statistics *falsealm_cnt = &(rtlpriv->falsealm_cnt);

	if (falsealm_cnt->cnt_all > digtable.fa_highthresh) {
		if ((digtable.backoff_val - 6) <
			digtable.backoffval_range_min)
			digtable.backoff_val = digtable.backoffval_range_min;
		else
			digtable.backoff_val -= 6;
	} else if (falsealm_cnt->cnt_all < digtable.fa_lowthresh) {
		if ((digtable.backoff_val + 6) >
			digtable.backoffval_range_max)
			digtable.backoff_val =
				 digtable.backoffval_range_max;
		else
			digtable.backoff_val += 6;
	}
}

static void _rtl92s_dm_initial_gain_sta_beforeconnect(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct false_alarm_statistics *falsealm_cnt = &(rtlpriv->falsealm_cnt);
	static u8 initialized, force_write;
	u8 initial_gain = 0;

	if ((digtable.pre_sta_connectstate == digtable.cur_sta_connectstate) ||
		(digtable.cur_sta_connectstate == DIG_STA_BEFORE_CONNECT)) {
		if (digtable.cur_sta_connectstate == DIG_STA_BEFORE_CONNECT) {
			if (rtlpriv->psc.rfpwr_state != ERFON)
				return;

			if (digtable.backoff_enable_flag)
				rtl92s_backoff_enable_flag(hw);
			else
				digtable.backoff_val = DM_DIG_BACKOFF;

			if ((digtable.rssi_val + 10 - digtable.backoff_val) >
				digtable.rx_gain_range_max)
				digtable.cur_igvalue =
						digtable.rx_gain_range_max;
			else if ((digtable.rssi_val + 10 - digtable.backoff_val)
				 < digtable.rx_gain_range_min)
				digtable.cur_igvalue =
						digtable.rx_gain_range_min;
			else
				digtable.cur_igvalue = digtable.rssi_val + 10 -
						digtable.backoff_val;

			if (falsealm_cnt->cnt_all > 10000)
				digtable.cur_igvalue =
					 (digtable.cur_igvalue > 0x33) ?
					 digtable.cur_igvalue : 0x33;

			if (falsealm_cnt->cnt_all > 16000)
				digtable.cur_igvalue =
						 digtable.rx_gain_range_max;
		
		} else {
			
			return;
		}
	} else {
		
		digtable.dig_ext_port_stage = DIG_EXT_PORT_STAGE_MAX;
		rtl92s_phy_set_fw_cmd(hw, FW_CMD_DIG_ENABLE);

		digtable.backoff_val = DM_DIG_BACKOFF;
		digtable.cur_igvalue = rtlpriv->phy.default_initialgain[0];
		digtable.pre_igvalue = 0;
		return;
	}

	
	if (digtable.pre_igvalue != rtl_get_bbreg(hw, ROFDM0_XAAGCCORE1,
						  MASKBYTE0))
		force_write = 1;

	if ((digtable.pre_igvalue != digtable.cur_igvalue) ||
	    !initialized || force_write) {
		
		rtl92s_phy_set_fw_cmd(hw, FW_CMD_DIG_DISABLE);

		initial_gain = (u8)digtable.cur_igvalue;

		
		rtl_set_bbreg(hw, ROFDM0_XAAGCCORE1, MASKBYTE0, initial_gain);
		rtl_set_bbreg(hw, ROFDM0_XBAGCCORE1, MASKBYTE0, initial_gain);
		digtable.pre_igvalue = digtable.cur_igvalue;
		initialized = 1;
		force_write = 0;
	}
}

static void _rtl92s_dm_ctrl_initgain_bytwoport(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->mac80211.act_scanning)
		return;

	
	if (rtlpriv->mac80211.link_state >= MAC80211_LINKED ||
	    rtlpriv->mac80211.opmode == NL80211_IFTYPE_ADHOC)
		digtable.cur_sta_connectstate = DIG_STA_CONNECT;
	else
		digtable.cur_sta_connectstate = DIG_STA_DISCONNECT;

	digtable.rssi_val = rtlpriv->dm.undecorated_smoothed_pwdb;

	
	if (digtable.cur_sta_connectstate != DIG_STA_DISCONNECT) {
		if (digtable.dig_twoport_algorithm ==
		    DIG_TWO_PORT_ALGO_FALSE_ALARM) {
			digtable.dig_twoport_algorithm = DIG_TWO_PORT_ALGO_RSSI;
			rtl92s_phy_set_fw_cmd(hw, FW_CMD_DIG_MODE_SS);
		}
	}

	_rtl92s_dm_false_alarm_counter_statistics(hw);
	_rtl92s_dm_initial_gain_sta_beforeconnect(hw);

	digtable.pre_sta_connectstate = digtable.cur_sta_connectstate;
}

static void _rtl92s_dm_ctrl_initgain_byrssi(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);

	
	if (rtlphy->rf_type == RF_2T2R)
		return;

	if (!rtlpriv->dm.dm_initialgain_enable)
		return;

	if (digtable.dig_enable_flag == false)
		return;

	_rtl92s_dm_ctrl_initgain_bytwoport(hw);
}

static void _rtl92s_dm_dynamic_txpower(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	long undecorated_smoothed_pwdb;
	long txpwr_threshold_lv1, txpwr_threshold_lv2;

	
	if (rtlphy->rf_type == RF_2T2R)
		return;

	if (!rtlpriv->dm.dynamic_txpower_enable ||
	    rtlpriv->dm.dm_flag & HAL_DM_HIPWR_DISABLE) {
		rtlpriv->dm.dynamic_txhighpower_lvl = TX_HIGHPWR_LEVEL_NORMAL;
		return;
	}

	if ((mac->link_state < MAC80211_LINKED) &&
	    (rtlpriv->dm.entry_min_undecoratedsmoothed_pwdb == 0)) {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
			 "Not connected to any\n");

		rtlpriv->dm.dynamic_txhighpower_lvl = TX_HIGHPWR_LEVEL_NORMAL;

		rtlpriv->dm.last_dtp_lvl = TX_HIGHPWR_LEVEL_NORMAL;
		return;
	}

	if (mac->link_state >= MAC80211_LINKED) {
		if (mac->opmode == NL80211_IFTYPE_ADHOC) {
			undecorated_smoothed_pwdb =
			    rtlpriv->dm.entry_min_undecoratedsmoothed_pwdb;
			RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
				 "AP Client PWDB = 0x%lx\n",
				 undecorated_smoothed_pwdb);
		} else {
			undecorated_smoothed_pwdb =
			    rtlpriv->dm.undecorated_smoothed_pwdb;
			RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
				 "STA Default Port PWDB = 0x%lx\n",
				 undecorated_smoothed_pwdb);
		}
	} else {
		undecorated_smoothed_pwdb =
		    rtlpriv->dm.entry_min_undecoratedsmoothed_pwdb;

		RT_TRACE(rtlpriv, COMP_POWER, DBG_LOUD,
			 "AP Ext Port PWDB = 0x%lx\n",
			 undecorated_smoothed_pwdb);
	}

	txpwr_threshold_lv2 = TX_POWER_NEAR_FIELD_THRESH_LVL2;
	txpwr_threshold_lv1 = TX_POWER_NEAR_FIELD_THRESH_LVL1;

	if (rtl_get_bbreg(hw, 0xc90, MASKBYTE0) == 1)
		rtlpriv->dm.dynamic_txhighpower_lvl = TX_HIGHPWR_LEVEL_NORMAL;
	else if (undecorated_smoothed_pwdb >= txpwr_threshold_lv2)
		rtlpriv->dm.dynamic_txhighpower_lvl = TX_HIGHPWR_LEVEL_NORMAL2;
	else if ((undecorated_smoothed_pwdb < (txpwr_threshold_lv2 - 3)) &&
		(undecorated_smoothed_pwdb >= txpwr_threshold_lv1))
		rtlpriv->dm.dynamic_txhighpower_lvl = TX_HIGHPWR_LEVEL_NORMAL1;
	else if (undecorated_smoothed_pwdb < (txpwr_threshold_lv1 - 3))
		rtlpriv->dm.dynamic_txhighpower_lvl = TX_HIGHPWR_LEVEL_NORMAL;

	if ((rtlpriv->dm.dynamic_txhighpower_lvl != rtlpriv->dm.last_dtp_lvl))
		rtl92s_phy_set_txpower(hw, rtlphy->current_channel);

	rtlpriv->dm.last_dtp_lvl = rtlpriv->dm.dynamic_txhighpower_lvl;
}

static void _rtl92s_dm_init_dig(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	
	digtable.dig_enable_flag = true;
	digtable.backoff_enable_flag = true;

	if ((rtlpriv->dm.dm_type == DM_TYPE_BYDRIVER) &&
	    (hal_get_firmwareversion(rtlpriv) >= 0x3c))
		digtable.dig_algorithm = DIG_ALGO_BY_TOW_PORT;
	else
		digtable.dig_algorithm =
			 DIG_ALGO_BEFORE_CONNECT_BY_RSSI_AND_ALARM;

	digtable.dig_twoport_algorithm = DIG_TWO_PORT_ALGO_RSSI;
	digtable.dig_ext_port_stage = DIG_EXT_PORT_STAGE_MAX;
	
	digtable.dig_dbgmode = DM_DBG_OFF;
	digtable.dig_slgorithm_switch = 0;

	
	digtable.dig_state = DM_STA_DIG_MAX;
	digtable.dig_highpwrstate = DM_STA_DIG_MAX;

	digtable.cur_sta_connectstate = DIG_STA_DISCONNECT;
	digtable.pre_sta_connectstate = DIG_STA_DISCONNECT;
	digtable.cur_ap_connectstate = DIG_AP_DISCONNECT;
	digtable.pre_ap_connectstate = DIG_AP_DISCONNECT;

	digtable.rssi_lowthresh = DM_DIG_THRESH_LOW;
	digtable.rssi_highthresh = DM_DIG_THRESH_HIGH;

	digtable.fa_lowthresh = DM_FALSEALARM_THRESH_LOW;
	digtable.fa_highthresh = DM_FALSEALARM_THRESH_HIGH;

	digtable.rssi_highpower_lowthresh = DM_DIG_HIGH_PWR_THRESH_LOW;
	digtable.rssi_highpower_highthresh = DM_DIG_HIGH_PWR_THRESH_HIGH;

	
	digtable.rssi_val = 50;
	digtable.backoff_val = DM_DIG_BACKOFF;
	digtable.rx_gain_range_max = DM_DIG_MAX;

	digtable.rx_gain_range_min = DM_DIG_MIN;

	digtable.backoffval_range_max = DM_DIG_BACKOFF_MAX;
	digtable.backoffval_range_min = DM_DIG_BACKOFF_MIN;
}

static void _rtl92s_dm_init_dynamic_txpower(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if ((hal_get_firmwareversion(rtlpriv) >= 60) &&
	    (rtlpriv->dm.dm_type == DM_TYPE_BYDRIVER))
		rtlpriv->dm.dynamic_txpower_enable = true;
	else
		rtlpriv->dm.dynamic_txpower_enable = false;

	rtlpriv->dm.last_dtp_lvl = TX_HIGHPWR_LEVEL_NORMAL;
	rtlpriv->dm.dynamic_txhighpower_lvl = TX_HIGHPWR_LEVEL_NORMAL;
}

void rtl92s_dm_init(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->dm.dm_type = DM_TYPE_BYDRIVER;
	rtlpriv->dm.undecorated_smoothed_pwdb = -1;

	_rtl92s_dm_init_dynamic_txpower(hw);
	rtl92s_dm_init_edca_turbo(hw);
	_rtl92s_dm_init_rate_adaptive_mask(hw);
	_rtl92s_dm_init_txpowertracking_thermalmeter(hw);
	_rtl92s_dm_init_dig(hw);

	rtl_write_dword(rtlpriv, WFM5, FW_CCA_CHK_ENABLE);
}

void rtl92s_dm_watchdog(struct ieee80211_hw *hw)
{
	_rtl92s_dm_check_edca_turbo(hw);
	_rtl92s_dm_check_txpowertracking_thermalmeter(hw);
	_rtl92s_dm_ctrl_initgain_byrssi(hw);
	_rtl92s_dm_dynamic_txpower(hw);
	_rtl92s_dm_refresh_rateadaptive_mask(hw);
	_rtl92s_dm_switch_baseband_mrc(hw);
}
