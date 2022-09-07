/*
 *
 * Copyright 2013-2022 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */

#include "srsgnb/asn1/f1ap.h"
#include "srsgnb/pcap/f1ap_pcap.h"
#include "srsgnb/support/test_utils.h"
#include <gtest/gtest.h>

using namespace asn1;

#define JSON_OUTPUT 1

class asn1_f1ap_test : public ::testing::Test
{
protected:
  void SetUp() override
  {
    srslog::fetch_basic_logger("ASN1").set_level(srslog::basic_levels::debug);
    srslog::fetch_basic_logger("ASN1").set_hex_dump_max_size(-1);

    test_logger.set_level(srslog::basic_levels::debug);
    test_logger.set_hex_dump_max_size(-1);

    srslog::init();

    pcap_writer.open("f1ap.pcap");

    // Start the log backend.
    srslog::init();
  }

  void TearDown() override
  {
    // flush logger after each test
    srslog::flush();

    pcap_writer.close();
  }

  srsgnb::f1ap_pcap     pcap_writer;
  srslog::basic_logger& test_logger = srslog::fetch_basic_logger("TEST");
};

// initiating message F1SetupRequest
TEST_F(asn1_f1ap_test, when_setup_message_correct_then_packing_successful)
{
  asn1::f1ap::f1_ap_pdu_c pdu;

  pdu.set_init_msg();
  pdu.init_msg().load_info_obj(ASN1_F1AP_ID_F1_SETUP);

  auto& setup_req                 = pdu.init_msg().value.f1_setup_request();
  setup_req->transaction_id.value = 99;
  setup_req->gnb_du_id.value      = 0x11;
  setup_req->gnb_du_name_present  = true;
  setup_req->gnb_du_name.value.from_string("srsDU");
  setup_req->gnb_du_rrc_version.value.latest_rrc_version.from_number(1);

  setup_req->gnb_du_served_cells_list_present = true;
  setup_req->gnb_du_served_cells_list.id      = ASN1_F1AP_ID_G_NB_DU_SERVED_CELLS_LIST;
  setup_req->gnb_du_served_cells_list.crit    = crit_opts::reject;

  asn1::protocol_ie_single_container_s<asn1::f1ap::gnb_du_served_cells_item_ies_o> served_cells_item_container = {};
  served_cells_item_container.set_item(ASN1_F1AP_ID_GNB_DU_SERVED_CELLS_ITEM);

  auto& served_cells_item = served_cells_item_container.value().gnb_du_served_cells_item();
  served_cells_item.served_cell_info.nrcgi.plmn_id.from_string("208991");
  served_cells_item.served_cell_info.nrcgi.nrcell_id.from_number(12345678);
  served_cells_item.served_cell_info.nrpci               = 0;
  served_cells_item.served_cell_info.five_gs_tac_present = true;
  served_cells_item.served_cell_info.five_gs_tac.from_number(1);

  asn1::f1ap::served_plmns_item_s served_plmn;
  served_plmn.plmn_id.from_string("208991");
  asn1::protocol_ext_field_s<asn1::f1ap::served_plmns_item_ext_ies_o> plmn_ext_container = {};
  plmn_ext_container.set_item(ASN1_F1AP_ID_TAI_SLICE_SUPPORT_LIST);
  auto&                            tai_slice_support_list = plmn_ext_container.value().tai_slice_support_list();
  asn1::f1ap::slice_support_item_s slice_support_item;
  slice_support_item.snssai.sst.from_number(1);
  tai_slice_support_list.push_back(slice_support_item);
  served_plmn.ie_exts.push_back(plmn_ext_container);
  served_cells_item.served_cell_info.served_plmns.push_back(served_plmn);

  served_cells_item.served_cell_info.nr_mode_info.set_tdd();
  served_cells_item.served_cell_info.nr_mode_info.tdd().nrfreq_info.nrarfcn = 626748;
  asn1::f1ap::freq_band_nr_item_s freq_band_nr_item;
  freq_band_nr_item.freq_band_ind_nr = 78;
  served_cells_item.served_cell_info.nr_mode_info.tdd().nrfreq_info.freq_band_list_nr.push_back(freq_band_nr_item);
  served_cells_item.served_cell_info.nr_mode_info.tdd().tx_bw.nrscs.value = asn1::f1ap::nrscs_opts::scs30;
  served_cells_item.served_cell_info.nr_mode_info.tdd().tx_bw.nrnrb.value = asn1::f1ap::nrnrb_opts::nrb51;
  served_cells_item.served_cell_info.meas_timing_cfg.from_string("30");

  served_cells_item.gnb_du_sys_info_present = true;
  served_cells_item.gnb_du_sys_info.mib_msg.from_string("01c586");
  served_cells_item.gnb_du_sys_info.sib1_msg.from_string(
      "92002808241099000001000000000a4213407800008c98d6d8d7f616e0804000020107e28180008000088a0dc7008000088a0007141a22"
      "81c874cc00020000232d5c6b6c65462001ec4cc5fc9c0493946a98d4d1e99355c00a1aba010580ec024646f62180");

  setup_req->gnb_du_served_cells_list.value.push_back(served_cells_item_container);

  srsgnb::byte_buffer tx_buffer;
  asn1::bit_ref       bref(tx_buffer);
  ASSERT_EQ(pdu.pack(bref), SRSASN_SUCCESS);

  ASSERT_EQ(test_pack_unpack_consistency(pdu), SRSASN_SUCCESS);

  std::vector<uint8_t> bytes{tx_buffer.begin(), tx_buffer.end()};
#if JSON_OUTPUT
  asn1::json_writer json_writer1;
  pdu.to_json(json_writer1);
  test_logger.info(
      bytes.data(), bytes.size(), "F1AP PDU unpacked ({} B): \n {}", bytes.size(), json_writer1.to_string().c_str());
#endif
  pcap_writer.write_pdu(bytes);
}

// successful outcome F1SetupResponse
TEST_F(asn1_f1ap_test, when_setup_response_correct_then_packing_successful)
{
  asn1::f1ap::f1_ap_pdu_c pdu;

  pdu.set_successful_outcome();
  pdu.successful_outcome().load_info_obj(ASN1_F1AP_ID_F1_SETUP);

  auto& setup_res                 = pdu.successful_outcome().value.f1_setup_resp();
  setup_res->transaction_id.value = 99;
  setup_res->gnb_cu_name_present  = true;
  setup_res->gnb_cu_name.value.from_string("srsCU");
  setup_res->gnb_cu_rrc_version.value.latest_rrc_version.from_number(2);

  srsgnb::byte_buffer tx_pdu;
  asn1::bit_ref       bref(tx_pdu);
  ASSERT_EQ(pdu.pack(bref), SRSASN_SUCCESS);

  ASSERT_EQ(test_pack_unpack_consistency(pdu), SRSASN_SUCCESS);

  std::vector<uint8_t> tx_buffer{tx_pdu.begin(), tx_pdu.end()};
#if JSON_OUTPUT
  asn1::json_writer json_writer1;
  pdu.to_json(json_writer1);
  test_logger.info(tx_buffer.data(),
                   tx_buffer.size(),
                   "F1AP PDU unpacked ({} B): \n {}",
                   tx_buffer.size(),
                   json_writer1.to_string().c_str());
#endif
  pcap_writer.write_pdu(srsgnb::span<uint8_t>(tx_buffer.data(), tx_buffer.size()));
}

// unsuccessful outcome F1SetupFailure
TEST_F(asn1_f1ap_test, when_setup_failure_correct_then_packing_successful)
{
  asn1::f1ap::f1_ap_pdu_c pdu;

  pdu.set_unsuccessful_outcome();
  pdu.unsuccessful_outcome().load_info_obj(ASN1_F1AP_ID_F1_SETUP);

  auto& setup_fail                 = pdu.unsuccessful_outcome().value.f1_setup_fail();
  setup_fail->transaction_id.value = 99;
  setup_fail->cause.value.set_radio_network();
  setup_fail->cause.value.radio_network() =
      asn1::f1ap::cause_radio_network_opts::options::unknown_or_already_allocated_gnb_cu_ue_f1ap_id;
  setup_fail->time_to_wait_present = true;
  setup_fail->time_to_wait.value   = asn1::f1ap::time_to_wait_opts::v10s;
  // add critical diagnostics

  srsgnb::byte_buffer tx_pdu;
  asn1::bit_ref       bref(tx_pdu);
  ASSERT_EQ(pdu.pack(bref), SRSASN_SUCCESS);

  ASSERT_EQ(test_pack_unpack_consistency(pdu), SRSASN_SUCCESS);

  std::vector<uint8_t> tx_buffer{tx_pdu.begin(), tx_pdu.end()};
#if JSON_OUTPUT
  asn1::json_writer json_writer1;
  pdu.to_json(json_writer1);
  test_logger.info(tx_buffer.data(),
                   tx_buffer.size(),
                   "F1AP PDU unpacked ({} B): \n {}",
                   tx_buffer.size(),
                   json_writer1.to_string().c_str());
#endif
  pcap_writer.write_pdu(srsgnb::span<uint8_t>(tx_buffer.data(), tx_buffer.size()));
}

TEST_F(asn1_f1ap_test, when_ue_context_setup_request_correct_then_unpacking_successful)
{
  uint8_t rx_msg[] = {
      0x00, 0x05, 0x00, 0x82, 0x73, 0x00, 0x00, 0x06, 0x00, 0x28, 0x00, 0x02, 0x00, 0x34, 0x00, 0x3f, 0x00, 0x09, 0x00,
      0x00, 0xf1, 0x10, 0x07, 0x53, 0x04, 0x04, 0x70, 0x00, 0x6b, 0x00, 0x01, 0x04, 0x00, 0x09, 0x00, 0x82, 0x20, 0x60,
      0x81, 0x1c, 0x18, 0x80, 0x80, 0xff, 0x23, 0x01, 0x05, 0x7a, 0x35, 0x60, 0xa6, 0x13, 0x00, 0x00, 0x60, 0x40, 0x1c,
      0x4d, 0x00, 0x00, 0x00, 0x07, 0xf0, 0x00, 0x00, 0x00, 0x03, 0x09, 0x80, 0x00, 0x30, 0x20, 0x0e, 0x26, 0x80, 0x00,
      0x01, 0x03, 0xf8, 0x00, 0x00, 0x00, 0x01, 0x84, 0xc0, 0x00, 0x18, 0x10, 0x07, 0x13, 0x40, 0x00, 0x01, 0x01, 0xfc,
      0x00, 0x00, 0x00, 0x00, 0xc1, 0x60, 0x40, 0x1c, 0x4d, 0x00, 0x00, 0x06, 0x07, 0xf0, 0x00, 0x00, 0x00, 0x03, 0x05,
      0x80, 0x00, 0x71, 0x34, 0x00, 0x00, 0x18, 0x1f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x10, 0x02, 0x00, 0x10, 0x13, 0x45,
      0xa2, 0xd1, 0x40, 0x31, 0x00, 0x00, 0x40, 0x20, 0x00, 0x10, 0x08, 0x08, 0x01, 0x00, 0x22, 0x00, 0x01, 0x00, 0x40,
      0x00, 0x20, 0x10, 0x10, 0x02, 0x00, 0x44, 0x00, 0x01, 0x00, 0x80, 0x00, 0x80, 0x20, 0x20, 0x04, 0x00, 0x84, 0x00,
      0x02, 0x01, 0x01, 0x00, 0x20, 0x04, 0x1e, 0x3f, 0xc0, 0x1c, 0x00, 0x11, 0xe4, 0xb2, 0xef, 0x97, 0xff, 0xd7, 0xc9,
      0x0a, 0x10, 0x00, 0x00, 0x30, 0x10, 0x20, 0x00, 0xe7, 0xc0, 0x00, 0xde, 0xa0, 0x4d, 0x00, 0xe7, 0xad, 0xfe, 0x1c,
      0xf5, 0xff, 0xef, 0xe4, 0x00, 0x00, 0x7d, 0x48, 0x08, 0x62, 0x89, 0x29, 0x4c, 0x8d, 0xff, 0xf7, 0x8d, 0xff, 0xf5,
      0xff, 0xfb, 0xfe, 0xfe, 0x00, 0x00, 0x00, 0x18, 0x05, 0xc3, 0x80, 0x80, 0xc0, 0x7e, 0x06, 0x01, 0x80, 0x3c, 0x04,
      0x06, 0x03, 0x9d, 0xff, 0x01, 0x01, 0x80, 0xe4, 0xff, 0xcd, 0x9f, 0xfb, 0x0c, 0x04, 0x08, 0x00, 0x39, 0xfe, 0x48,
      0x50, 0x80, 0x00, 0x01, 0x80, 0x81, 0x00, 0x07, 0x3e, 0xf0, 0x02, 0xec, 0xe0, 0x00, 0x00, 0x58, 0xf0, 0x00, 0x03,
      0x95, 0x34, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf2, 0xa8, 0x20, 0x00, 0x09, 0x27, 0x63, 0x00, 0x27,
      0xfa, 0x90, 0x1f, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x20, 0x00, 0x36, 0x59, 0x07, 0xe1,
      0x0c, 0x80, 0xff, 0x23, 0x01, 0x05, 0x7a, 0x35, 0x60, 0xa6, 0x13, 0x00, 0x00, 0x60, 0x40, 0x1c, 0x4d, 0x00, 0x00,
      0x00, 0x07, 0xf0, 0x00, 0x00, 0x00, 0x03, 0x09, 0x80, 0x00, 0x30, 0x20, 0x0e, 0x26, 0x80, 0x00, 0x01, 0x03, 0xf8,
      0x00, 0x00, 0x00, 0x01, 0x84, 0xc0, 0x00, 0x18, 0x10, 0x07, 0x13, 0x40, 0x00, 0x01, 0x01, 0xfc, 0x00, 0x00, 0x00,
      0x00, 0xc1, 0x60, 0x40, 0x1c, 0x4d, 0x00, 0x00, 0x06, 0x07, 0xf0, 0x00, 0x00, 0x00, 0x03, 0x05, 0x80, 0x00, 0x71,
      0x34, 0x00, 0x00, 0x18, 0x1f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x10, 0x02, 0x00, 0x10, 0x13, 0x45, 0xa2, 0xd1, 0x40,
      0x31, 0x00, 0x00, 0x40, 0x20, 0x00, 0x10, 0x08, 0x08, 0x01, 0x00, 0x22, 0x00, 0x01, 0x00, 0x40, 0x00, 0x20, 0x10,
      0x10, 0x02, 0x00, 0x44, 0x00, 0x01, 0x00, 0x80, 0x00, 0x80, 0x20, 0x20, 0x04, 0x00, 0x84, 0x00, 0x02, 0x01, 0x01,
      0x00, 0x20, 0x04, 0x1e, 0x3f, 0xc0, 0x1c, 0x00, 0x11, 0xe4, 0xb2, 0xef, 0x97, 0xff, 0xd7, 0xc9, 0x0a, 0x10, 0x00,
      0x00, 0x30, 0x10, 0x20, 0x00, 0xe7, 0xc0, 0x00, 0xde, 0xa0, 0x4d, 0x00, 0xe7, 0xad, 0xfe, 0x1c, 0xf5, 0xff, 0xef,
      0xe4, 0x00, 0x00, 0x7d, 0x48, 0x08, 0x62, 0x89, 0x29, 0x4c, 0x8d, 0xff, 0xf7, 0x8d, 0xff, 0xf5, 0xff, 0xfb, 0xfe,
      0xfe, 0x00, 0x00, 0x00, 0x18, 0x05, 0xc3, 0x80, 0x80, 0xc0, 0x7e, 0x06, 0x01, 0x80, 0x3c, 0x04, 0x06, 0x03, 0x9d,
      0xff, 0x01, 0x01, 0x80, 0xe4, 0xff, 0xcd, 0x9f, 0xfb, 0x0c, 0x04, 0x08, 0x00, 0x39, 0xfe, 0x48, 0x50, 0x80, 0x00,
      0x01, 0x80, 0x81, 0x00, 0x07, 0x3e, 0xf0, 0x02, 0xec, 0xe0, 0x00, 0x00, 0x58, 0xf0, 0x00, 0x03, 0x95, 0x34, 0x00,
      0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf2, 0xa8, 0x20, 0x00, 0x00, 0x23, 0x00, 0x27, 0x00, 0x00, 0x22, 0x00,
      0x22, 0x51, 0x40, 0x08, 0x09, 0x00, 0xfe, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x20, 0x66, 0x00, 0x17, 0x02, 0x00, 0x00, 0x00, 0xa1, 0x40, 0x01, 0x40, 0x00, 0x9e, 0x40,
      0x04, 0x20, 0x20, 0x3a, 0x00};
  srsgnb::byte_buffer rx_pdu{rx_msg};

  pcap_writer.write_pdu(rx_msg);

  asn1::cbit_ref          bref{rx_pdu};
  asn1::f1ap::f1_ap_pdu_c pdu;

  ASSERT_EQ(pdu.unpack(bref), SRSASN_SUCCESS);
  ASSERT_EQ(asn1::f1ap::f1_ap_pdu_c::types_opts::init_msg, pdu.type());

  ASSERT_EQ(pdu.init_msg().proc_code, ASN1_F1AP_ID_UE_CONTEXT_SETUP);
  ASSERT_EQ(pdu.init_msg().value.type(),
            asn1::f1ap::f1_ap_elem_procs_o::init_msg_c::types_opts::ue_context_setup_request);

  ASSERT_EQ(test_pack_unpack_consistency(pdu), SRSASN_SUCCESS);

#if JSON_OUTPUT
  int               unpacked_len = bref.distance_bytes();
  asn1::json_writer json_writer1;
  pdu.to_json(json_writer1);
  test_logger.info(
      rx_msg, unpacked_len, "F1AP PDU unpacked ({} B): \n {}", unpacked_len, json_writer1.to_string().c_str());
#endif
}

TEST_F(asn1_f1ap_test, when_initial_ul_rrc_message_transfer_correct_then_unpacking_successful)
{
  uint8_t rx_msg[] = {
      0x00, 0x0b, 0x40, 0x80, 0xa4, 0x00, 0x00, 0x06, 0x00, 0x29, 0x00, 0x03, 0x40, 0xa1, 0x27, 0x00, 0x6f, 0x00, 0x09,
      0x00, 0x02, 0xf8, 0x99, 0x00, 0x0b, 0xc6, 0x14, 0xe0, 0x00, 0x5f, 0x00, 0x03, 0x00, 0xa1, 0x27, 0x00, 0x32, 0x00,
      0x07, 0x06, 0x1d, 0xec, 0x89, 0xd0, 0x57, 0x66, 0x00, 0x80, 0x00, 0x71, 0x70, 0x5c, 0x00, 0xb0, 0x01, 0x11, 0x7a,
      0xec, 0x70, 0x10, 0x61, 0xe0, 0x00, 0x7c, 0x20, 0x40, 0x8d, 0x07, 0x81, 0x00, 0x20, 0xa2, 0x09, 0x04, 0x80, 0xca,
      0x80, 0x00, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x83, 0x70, 0x84, 0x20, 0x00, 0x08, 0x81, 0x65, 0x00, 0x00, 0x48,
      0x20, 0x00, 0x02, 0x06, 0x9a, 0x06, 0xaa, 0x49, 0x88, 0x00, 0x02, 0x00, 0x20, 0x40, 0x00, 0x40, 0x0d, 0x00, 0x80,
      0x13, 0xb6, 0x4b, 0x18, 0x14, 0x40, 0x0e, 0x46, 0x8a, 0xcf, 0x12, 0x00, 0x00, 0x09, 0x60, 0x70, 0x82, 0x0f, 0x17,
      0x7e, 0x06, 0x08, 0x70, 0x00, 0x00, 0x00, 0xe2, 0x50, 0x38, 0x00, 0x00, 0x40, 0xbd, 0xe8, 0x02, 0x00, 0x04, 0x00,
      0x00, 0x00, 0x00, 0x02, 0x82, 0x01, 0x95, 0x03, 0x00, 0xc4, 0x00, 0x00, 0x4e, 0x40, 0x02, 0x00, 0x00};
  srsgnb::byte_buffer rx_pdu{rx_msg};

  pcap_writer.write_pdu(rx_msg);

  asn1::cbit_ref          bref{rx_pdu};
  asn1::f1ap::f1_ap_pdu_c pdu;

  ASSERT_EQ(pdu.unpack(bref), SRSASN_SUCCESS);
  ASSERT_EQ(pdu.type(), asn1::f1ap::f1_ap_pdu_c::types_opts::init_msg);

  ASSERT_EQ(pdu.init_msg().proc_code, ASN1_F1AP_ID_INIT_ULRRC_MSG_TRANSFER);
  ASSERT_EQ(pdu.init_msg().value.type(),
            asn1::f1ap::f1_ap_elem_procs_o::init_msg_c::types_opts::init_ulrrc_msg_transfer);

  ASSERT_EQ(test_pack_unpack_consistency(pdu), SRSASN_SUCCESS);

#if JSON_OUTPUT
  int               unpacked_len = bref.distance_bytes();
  asn1::json_writer json_writer1;
  pdu.to_json(json_writer1);
  test_logger.info(
      rx_msg, unpacked_len, "F1AP PDU unpacked ({} B): \n {}", unpacked_len, json_writer1.to_string().c_str());
#endif
}

// repack manually
TEST_F(asn1_f1ap_test, when_initial_ul_rrc_message_transfer_packing_correct_then_equal_to_original_TV)
{
  uint8_t rx_msg[] = {
      0x00, 0x0b, 0x40, 0x80, 0xa4, 0x00, 0x00, 0x06, 0x00, 0x29, 0x00, 0x03, 0x40, 0xa1, 0x27, 0x00, 0x6f, 0x00, 0x09,
      0x00, 0x02, 0xf8, 0x99, 0x00, 0x0b, 0xc6, 0x14, 0xe0, 0x00, 0x5f, 0x00, 0x03, 0x00, 0xa1, 0x27, 0x00, 0x32, 0x00,
      0x07, 0x06, 0x1d, 0xec, 0x89, 0xd0, 0x57, 0x66, 0x00, 0x80, 0x00, 0x71, 0x70, 0x5c, 0x00, 0xb0, 0x01, 0x11, 0x7a,
      0xec, 0x70, 0x10, 0x61, 0xe0, 0x00, 0x7c, 0x20, 0x40, 0x8d, 0x07, 0x81, 0x00, 0x20, 0xa2, 0x09, 0x04, 0x80, 0xca,
      0x80, 0x00, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x83, 0x70, 0x84, 0x20, 0x00, 0x08, 0x81, 0x65, 0x00, 0x00, 0x48,
      0x20, 0x00, 0x02, 0x06, 0x9a, 0x06, 0xaa, 0x49, 0x88, 0x00, 0x02, 0x00, 0x20, 0x40, 0x00, 0x40, 0x0d, 0x00, 0x80,
      0x13, 0xb6, 0x4b, 0x18, 0x14, 0x40, 0x0e, 0x46, 0x8a, 0xcf, 0x12, 0x00, 0x00, 0x09, 0x60, 0x70, 0x82, 0x0f, 0x17,
      0x7e, 0x06, 0x08, 0x70, 0x00, 0x00, 0x00, 0xe2, 0x50, 0x38, 0x00, 0x00, 0x40, 0xbd, 0xe8, 0x02, 0x00, 0x04, 0x00,
      0x00, 0x00, 0x00, 0x02, 0x82, 0x01, 0x95, 0x03, 0x00, 0xc4, 0x00, 0x00, 0x4e, 0x40, 0x02, 0x00, 0x00};
  srsgnb::byte_buffer rx_pdu{rx_msg};

  asn1::f1ap::f1_ap_pdu_c tx_pdu;

  tx_pdu.set_init_msg();
  tx_pdu.init_msg().load_info_obj(ASN1_F1AP_ID_INIT_ULRRC_MSG_TRANSFER);

  auto& init_ul_rrc                     = tx_pdu.init_msg().value.init_ulrrc_msg_transfer();
  init_ul_rrc->gnb_du_ue_f1_ap_id.value = 41255; // same as C-RNTI

  init_ul_rrc->nrcgi.value.nrcell_id.from_string("000000000000101111000110000101001110");
  init_ul_rrc->nrcgi.value.plmn_id.from_string("02f899");
  init_ul_rrc->c_rnti.value = 41255;

  init_ul_rrc->rrc_container.value.from_string("1dec89d05766");
  init_ul_rrc->duto_currc_container_present = true;
  init_ul_rrc->duto_currc_container.value.from_string(
      "5c00b001117aec701061e0007c20408d07810020a2090480ca8000f800000000008370842000088165000048200002069a06aa49880002"
      "00204000400d008013b64b1814400e468acf120000096070820f177e060870000000e25038000040bde802000400000000028201950300"
      "c400");

  srsgnb::byte_buffer tx_buffer;
  asn1::bit_ref       bref_tx(tx_buffer);
  ASSERT_EQ(tx_pdu.pack(bref_tx), SRSASN_SUCCESS);

  // compare against original TV
  ASSERT_EQ(tx_buffer.length(), sizeof(rx_msg));
  ASSERT_EQ(rx_pdu, tx_buffer);
}
