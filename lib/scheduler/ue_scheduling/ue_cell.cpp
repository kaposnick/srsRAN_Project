/*
 *
 * Copyright 2021-2023 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "ue_cell.h"
#include "../support/dmrs_helpers.h"
#include "../support/mcs_calculator.h"
#include "../support/prbs_calculator.h"
#include "../support/sch_pdu_builder.h"
#include "srsran/scheduler/scheduler_feedback_handler.h"

using namespace srsran;

/// Number of UL HARQs reserved per UE (Implementation-defined)
constexpr unsigned NOF_UL_HARQS = 16;

ue_cell::ue_cell(du_ue_index_t                     ue_index_,
                 rnti_t                            crnti_val,
                 const scheduler_ue_expert_config& expert_cfg_,
                 const cell_configuration&         cell_cfg_common_,
                 const serving_cell_config&        ue_serv_cell,
                 ue_harq_timeout_notifier          harq_timeout_notifier) :
  ue_index(ue_index_),
  cell_index(ue_serv_cell.cell_index),
  harqs(crnti_val, (unsigned)ue_serv_cell.pdsch_serv_cell_cfg->nof_harq_proc, NOF_UL_HARQS, harq_timeout_notifier),
  crnti_(crnti_val),
  expert_cfg(expert_cfg_),
  ue_cfg(cell_cfg_common_, ue_serv_cell),
  logger(srslog::fetch_basic_logger("SCHED")),
  // PCell starts in fallback mode.
  is_fallback_mode(cell_cfg_common_.cell_index == to_du_cell_index(0))
{
  if (expert_cfg.ul_mcs.start() != expert_cfg.ul_mcs.stop()) {
    update_pusch_snr(expert_cfg.initial_ul_sinr);
  }
  ue_metrics.latest_wb_cqi = expert_cfg.initial_cqi;
}

void ue_cell::handle_reconfiguration_request(const serving_cell_config& new_ue_cell_cfg)
{
  ue_cfg.reconfigure(new_ue_cell_cfg);
}

void ue_cell::handle_csi_report(const uci_indication::uci_pdu::csi_report& csi)
{
  if (csi.cqi.has_value()) {
    ue_metrics.latest_wb_cqi = *csi.cqi;
  }
  if (csi.ri.has_value()) {
    ue_metrics.latest_ri = *csi.ri;
  }
  if (csi.pmi.has_value()) {
    ue_metrics.latest_pmi = *csi.pmi;
  }
}

grant_prbs_mcs ue_cell::required_dl_prbs(const pdsch_time_domain_resource_allocation& pdsch_td_cfg,
                                         unsigned                                     pending_bytes) const
{
  const cell_configuration& cell_cfg = cfg().cell_cfg_common;

  pdsch_config_params pdsch_cfg = get_pdsch_config_f1_0_c_rnti(ue_cfg, pdsch_td_cfg);

  // NOTE: This value is for preventing uninitialized variables, will be overwritten, no need to set it to a particular
  // value.
  sch_mcs_index mcs{0};
  if (expert_cfg.dl_mcs.start() == expert_cfg.dl_mcs.stop()) {
    mcs = expert_cfg.dl_mcs.start();
  } else {
    optional<sch_mcs_index> estimated_mcs = map_cqi_to_mcs(get_latest_wb_cqi(), pdsch_cfg.mcs_table);
    if (estimated_mcs.has_value()) {
      mcs = std::min(std::max(estimated_mcs.value(), expert_cfg.dl_mcs.start()), expert_cfg.dl_mcs.stop());
    } else {
      // Return a grant with no PRBs if the MCS is invalid (CQI is either 0, for UE out of range, or > 15).
      return grant_prbs_mcs{.n_prbs = 0};
    }
  }

  sch_mcs_description mcs_config = pdsch_mcs_get_config(cfg().cfg_dedicated().init_dl_bwp.pdsch_cfg->mcs_table, mcs);

  dmrs_information dmrs_info = make_dmrs_info_common(pdsch_td_cfg, cell_cfg.pci, cell_cfg.dmrs_typeA_pos);

  sch_prbs_tbs prbs_tbs = get_nof_prbs(prbs_calculator_sch_config{pending_bytes,
                                                                  (unsigned)pdsch_cfg.symbols.length(),
                                                                  calculate_nof_dmrs_per_rb(dmrs_info),
                                                                  pdsch_cfg.nof_oh_prb,
                                                                  mcs_config,
                                                                  pdsch_cfg.nof_layers});

  const bwp_downlink_common& bwp_dl_cmn = *ue_cfg.bwp(active_bwp_id()).dl_common;
  return grant_prbs_mcs{mcs, std::min(prbs_tbs.nof_prbs, bwp_dl_cmn.generic_params.crbs.length())};
}

grant_prbs_mcs ue_cell::required_ul_prbs(const pusch_time_domain_resource_allocation& pusch_td_cfg,
                                         unsigned                                     pending_bytes,
                                         dci_ul_rnti_config_type                      type) const
{
  const cell_configuration& cell_cfg = cfg().cell_cfg_common;

  const bwp_uplink_common& bwp_ul_cmn = *ue_cfg.bwp(active_bwp_id()).ul_common;

  pusch_config_params pusch_cfg;
  switch (type) {
    case dci_ul_rnti_config_type::tc_rnti_f0_0:
      pusch_cfg = get_pusch_config_f0_0_tc_rnti(cell_cfg, pusch_td_cfg);
      break;
    case dci_ul_rnti_config_type::c_rnti_f0_0:
      pusch_cfg = get_pusch_config_f0_0_c_rnti(ue_cfg, bwp_ul_cmn, pusch_td_cfg);
      break;
    case dci_ul_rnti_config_type::c_rnti_f0_1:
      pusch_cfg = get_pusch_config_f0_1_c_rnti(ue_cfg, pusch_td_cfg, get_nof_ul_layers());
      break;
    default:
      report_fatal_error("Unsupported PDCCH DCI UL format");
  }

  double        ul_snr{ue_metrics.pusch_snr_db};
  sch_mcs_index mcs{0};
  if (expert_cfg.ul_mcs.start() == expert_cfg.ul_mcs.stop()) {
    // Fixed MCS.
    mcs = expert_cfg.ul_mcs.start();
  } else {
    // MCS is estimated from SNR.
    mcs = map_snr_to_mcs_ul(ul_snr, pusch_cfg.mcs_table);
    mcs = std::min(std::max(mcs, expert_cfg.ul_mcs.start()), expert_cfg.ul_mcs.stop());
  }

  sch_mcs_description mcs_config = pusch_mcs_get_config(pusch_cfg.mcs_table, mcs, false);

  const unsigned nof_symbols = static_cast<unsigned>(pusch_td_cfg.symbols.length());

  sch_prbs_tbs prbs_tbs = get_nof_prbs(prbs_calculator_sch_config{pending_bytes,
                                                                  nof_symbols,
                                                                  calculate_nof_dmrs_per_rb(pusch_cfg.dmrs),
                                                                  pusch_cfg.nof_oh_prb,
                                                                  mcs_config,
                                                                  pusch_cfg.nof_layers});

  return grant_prbs_mcs{mcs, std::min(prbs_tbs.nof_prbs, bwp_ul_cmn.generic_params.crbs.length())};
}

int ue_cell::handle_crc_pdu(slot_point pusch_slot, const ul_crc_pdu_indication& crc_pdu)
{
  // Update UL HARQ state.
  int tbs = harqs.ul_crc_info(crc_pdu.harq_id, crc_pdu.tb_crc_success, pusch_slot);
  if (tbs >= 0) {
    // HARQ with matching ID and UCI slot was found.

    if (crc_pdu.tb_crc_success and is_fallback_mode) {
      logger.debug("ue={} rnti={}: Leaving fallback mode", ue_index, rnti());
      is_fallback_mode = false;
    }

    // Update PUSCH KO count metrics.
    ue_metrics.consecutive_pusch_kos = (crc_pdu.tb_crc_success) ? 0 : ue_metrics.consecutive_pusch_kos + 1;

    // Update PUSCH SNR reported from PHY.
    update_pusch_snr(crc_pdu.ul_sinr_metric);
  }

  return tbs;
}

static_vector<const search_space_info*, MAX_NOF_SEARCH_SPACE_PER_BWP>
ue_cell::get_active_search_spaces(bool is_dl) const
{
  // TODO: Set aggregation level based on link quality.
  static const aggregation_level agg_lvl      = aggregation_level::n4;
  static const unsigned          aggr_lvl_idx = to_aggregation_level_index(agg_lvl);

  static_vector<const search_space_info*, MAX_NOF_SEARCH_SPACE_PER_BWP> active_search_spaces;

  if (is_dl and is_fallback_mode) {
    // In fallback mode state, only use search spaces configured in CellConfigCommon.
    for (const search_space_configuration& ss :
         ue_cfg.cell_cfg_common.dl_cfg_common.init_dl_bwp.pdcch_common.search_spaces) {
      active_search_spaces.push_back(&ue_cfg.search_space(ss.id));
    }
    return active_search_spaces;
  }

  const auto& bwp_ss_lst = ue_cfg.bwp(active_bwp_id()).search_spaces;
  for (const search_space_info* search_space : bwp_ss_lst) {
    active_search_spaces.push_back(search_space);
  }
  // TODO: Revisit SearchSpace prioritization.
  std::sort(active_search_spaces.begin(),
            active_search_spaces.end(),
            [](const search_space_info* lhs, const search_space_info* rhs) -> bool {
              if (lhs->cfg->nof_candidates[aggr_lvl_idx] == rhs->cfg->nof_candidates[aggr_lvl_idx]) {
                // In case nof. candidates are equal, choose the SS with higher CORESET Id (i.e. try to use CORESET#0 as
                // little as possible).
                return lhs->cfg->cs_id > rhs->cfg->cs_id;
              }
              return lhs->cfg->nof_candidates[aggr_lvl_idx] > rhs->cfg->nof_candidates[aggr_lvl_idx];
            });

  return active_search_spaces;
}
