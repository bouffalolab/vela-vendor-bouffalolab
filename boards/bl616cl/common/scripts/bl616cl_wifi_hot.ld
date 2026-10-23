/****************************************************************************
 * vendor/bouffalolab/boards/bl616cl/common/scripts/bl616cl_wifi_hot.ld
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/* Wi-Fi hot paths in call order, so that they stay in few I-cache sets
 * and no cold code sits between them.  ld.script includes this list at
 * the start of .text, between __bl616cl_wifi_hot_start and
 * __bl616cl_wifi_hot_end.  Derived from macsw_cache_affinity.ld.in
 * (functions that LTO keeps separate in this build) plus the task loop
 * and host port functions that the tick profile shows on the same paths.
 * LTO renames local copies with a .lto_priv/.isra/.constprop suffix,
 * hence the ".*" patterns.  layout_check.py and perfmon_report.py read
 * one "*(.text.fn .text.fn.*)" entry per line.  Changes need the A/B in
 * docs/bl616cl-hot-code-layout.md.
 */

/* Wi-Fi task loop */
*(.text.wifi_main .text.wifi_main.*)
*(.text.ke_evt_schedule .text.ke_evt_schedule.*)
*(.text.ke_evt_clear .text.ke_evt_clear.*)
*(.text.ke_evt_set .text.ke_evt_set.*)
*(.text.ke_state_get .text.ke_state_get.*)
*(.text.wifi_task_suspend .text.wifi_task_suspend.*)
*(.text.wifi_task_resume .text.wifi_task_resume.*)
*(.text.coex_coord_on_wifi_suspend_enter .text.coex_coord_on_wifi_suspend_enter.*)
*(.text.coex_coord_on_wifi_wake .text.coex_coord_on_wifi_wake.*)
*(.text.coex_coord_set_tx_paused .text.coex_coord_set_tx_paused.*)
*(.text.co_list_push_back .text.co_list_push_back.*)
*(.text.co_list_push_front .text.co_list_push_front.*)
*(.text.co_list_pop_front .text.co_list_pop_front.*)
*(.text.co_clz .text.co_clz.*)
/* Wi-Fi TX */
*(.text.rtos_mutex_lock .text.rtos_mutex_lock.*)
*(.text.rtos_mutex_unlock .text.rtos_mutex_unlock.*)
*(.text.wl80211_mac_tx .text.wl80211_mac_tx.*)
*(.text.vif_mgmt_get_staid .text.vif_mgmt_get_staid.*)
*(.text.macif_tx_evt .text.macif_tx_evt.*)
*(.text.txu_cntrl_push .text.txu_cntrl_push.*)
*(.text.txl_cntrl_tx_check .text.txl_cntrl_tx_check.*)
*(.text.txl_payload_handle .text.txl_payload_handle.*)
*(.text.chan_is_tx_allowed .text.chan_is_tx_allowed.*)
*(.text.txu_cntrl_sechdr_len_compute .text.txu_cntrl_sechdr_len_compute.*)
*(.text.rc_check .text.rc_check.*)
*(.text.rc_trial_check_bfm_stbc_htc .text.rc_trial_check_bfm_stbc_htc.*)
*(.text.tpc_get_vif_tx_power_vs_rate .text.tpc_get_vif_tx_power_vs_rate.*)
*(.text.wl_wlan_power_cfg_get .text.wl_wlan_power_cfg_get.*)
*(.text.trpc_get_default_power_idx .text.trpc_get_default_power_idx.*)
*(.text.trpc_get_power_idx .text.trpc_get_power_idx.*)
*(.text.tpc_get_vif_tx_prot_power_vs_rate .text.tpc_get_vif_tx_prot_power_vs_rate.*)
*(.text.txu_cntrl_sec_hdr_append .text.txu_cntrl_sec_hdr_append.*)
*(.text.txl_ampdu_constraints_get .text.txl_ampdu_constraints_get.*)
*(.text.txl_agg_mpdu_nb_delims .text.txl_agg_mpdu_nb_delims.*)
*(.text.txl_agg_split .text.txl_agg_split.*)
*(.text.txl_he_mu_edca_blocked .text.txl_he_mu_edca_blocked.*)
*(.text.txl_agg_he_tb_prep_for_cat .text.txl_agg_he_tb_prep_for_cat.*)
*(.text.txl_he_txop_dur_rtscts_thres_ampdu_check .text.txl_he_txop_dur_rtscts_thres_ampdu_check.*)
*(.text.txl_he_tb_config .text.txl_he_tb_config.*)
*(.text.txl_he_tb_enable .text.txl_he_tb_enable.*)
*(.text.txl_he_txop_dur_rtscts_thres_mpdu_check .text.txl_he_txop_dur_rtscts_thres_mpdu_check.*)
*(.text.txl_he_txop_dur_rtscts_thres_apply .text.txl_he_txop_dur_rtscts_thres_apply.*)
*(.text.txl_frame_exchange_chain .text.txl_frame_exchange_chain.*)
*(.text.txl_cntrl_newhead .text.txl_cntrl_newhead.*)
*(.text.wlan_txavail .text.wlan_txavail.*)
*(.text.net_buf_tx_alloc .text.net_buf_tx_alloc.*)
*(.text.find_active_sta_vif .text.find_active_sta_vif.*)
*(.text.txu_cntrl_use_bss_min_rate .text.txu_cntrl_use_bss_min_rate.*)
*(.text.bam_baw_index_compute .text.bam_baw_index_compute.*)
*(.text.bam_baw_index_compute_opt .text.bam_baw_index_compute_opt.*)
*(.text.txl_fill_bar .text.txl_fill_bar.*)
. = ALIGN(64);
/* Wi-Fi TX confirmation */
*(.text.interrupt0_handler .text.interrupt0_handler.*)
*(.text.txl_agg_finish .text.txl_agg_finish.*)
*(.text.txl_transmit_trigger .text.txl_transmit_trigger.*)
*(.text.txl_agg_check_rtscts_retry_limit .text.txl_agg_check_rtscts_retry_limit.*)
*(.text.txl_agg_check_saved_agg_desc .text.txl_agg_check_saved_agg_desc.*)
*(.text.txl_frame_cfm .text.txl_frame_cfm.*)
*(.text.txl_cfm_push .text.txl_cfm_push.*)
*(.text.rxl_immediate_frame_get .text.rxl_immediate_frame_get.*)
*(.text.txl_frame_evt .text.txl_frame_evt.*)
*(.text.txu_cntrl_cfm .text.txu_cntrl_cfm.*)
*(.text.ke_msg_send .text.ke_msg_send.*)
*(.text.rc_cfm_ampdu .text.rc_cfm_ampdu.*)
*(.text.rc_update_counters .text.rc_update_counters.*)
*(.text.macif_tx_cfm_push .text.macif_tx_cfm_push.*)
*(.text.macif_tx_data_ind .text.macif_tx_data_ind.*)
*(.text.rc_cfm_singleton .text.rc_cfm_singleton.*)
*(.text.txl_agg_check .text.txl_agg_check.*)
*(.text.txl_cfm_evt .text.txl_cfm_evt.*)
*(.text.macif_msg_evt .text.macif_msg_evt.*)
*(.text.macsw_tx_pending_cb .text.macsw_tx_pending_cb.*)
*(.text.wl80211_sta_tx_complete .text.wl80211_sta_tx_complete.*)
*(.text.net_buf_tx_free .text.net_buf_tx_free.*)
. = ALIGN(64);
/* Wi-Fi RX */
*(.text.rxu_msdu_upload_and_indicate .text.rxu_msdu_upload_and_indicate.*)
*(.text.rxl_mpdu_transfer .text.rxl_mpdu_transfer.*)
*(.text.rxl_payload_transfer .text.rxl_payload_transfer.*)
*(.text.rxu_cntrl_desc_prepare .text.rxu_cntrl_desc_prepare.*)
*(.text.rxl_mpdu_isr .text.rxl_mpdu_isr.*)
*(.text.rxl_rxdesc_get .text.rxl_rxdesc_get.*)
*(.text.rxl_rxdesc_ready_for_processing .text.rxl_rxdesc_ready_for_processing.*)
*(.text.rxl_dma_int_handler .text.rxl_dma_int_handler.*)
*(.text.rxl_dma_evt .text.rxl_dma_evt.*)
*(.text.rxl_frame_release .text.rxl_frame_release.*)
*(.text.rxu_cntrl_frame_handle .text.rxu_cntrl_frame_handle.*)
*(.text.rxu_cntrl_protected_handle .text.rxu_cntrl_protected_handle.*)
*(.text.rxu_cntrl_check_pn .text.rxu_cntrl_check_pn.*)
*(.text.rxu_cntrl_duplicate_check .text.rxu_cntrl_duplicate_check.*)
*(.text.rxu_cntrl_reord_fwd .text.rxu_cntrl_reord_fwd.*)
*(.text.macif_rx_desc_upload .text.macif_rx_desc_upload.*)
*(.text.rxl_cntrl_evt .text.rxl_cntrl_evt.*)
*(.text.dma_push .text.dma_push.*)
*(.text.rxl_go_to_last_rbd .text.rxl_go_to_last_rbd.*)
*(.text.rxu_cntrl_mic_check .text.rxu_cntrl_mic_check.*)
*(.text.mac_ie_check .text.mac_ie_check.*)
*(.text.macif_rx_buf_get .text.macif_rx_buf_get.*)
*(.text.wl80211_mac_rx_free .text.wl80211_mac_rx_free.*)
*(.text.wlan_sta_rx_done .text.wlan_sta_rx_done.*)
/* NuttX semaphores, work queue and timers on the same paths */
*(.text.nxsem_wait_slow .text.nxsem_wait_slow.*)
*(.text.nxsem_post_slow .text.nxsem_post_slow.*)
*(.text.nxsem_tickwait_slow .text.nxsem_tickwait_slow.*)
*(.text.work_queue .text.work_queue.*)
*(.text.work_queue_wq .text.work_queue_wq.*)
*(.text.work_qcancel .text.work_qcancel.*)
*(.text.wd_cancel .text.wd_cancel.*)
*(.text.clock_systime_ticks .text.clock_systime_ticks.*)
