#
# Copyright 2021-2023 Software Radio Systems Limited
#
# By using this file, you agree to the terms and conditions set
# forth in the LICENSE file which can be found at the top level of
# the distribution.
#
"""
Steps related with stubs / resources
"""

import logging
from contextlib import suppress
from typing import Dict, Sequence, Tuple

import grpc
import pytest
from retina.launcher.artifacts import RetinaTestData
from retina.protocol import RanStub
from retina.protocol.base_pb2 import (
    Empty,
    Metrics,
    PingRequest,
    PingResponse,
    StartInfo,
    StopResponse,
    String,
    UEDefinition,
    UInteger,
)
from retina.protocol.fivegc_pb2 import IPerfResponse
from retina.protocol.fivegc_pb2_grpc import FiveGCStub
from retina.protocol.gnb_pb2 import GNBStartInfo
from retina.protocol.gnb_pb2_grpc import GNBStub
from retina.protocol.ue_pb2 import IPerfDir, IPerfProto, IPerfRequest, UEAttachedInfo, UEStartInfo
from retina.protocol.ue_pb2_grpc import UEStub

RF_MAX_TIMEOUT: int = 3 * 60  # Time enough in RF when loading a new image in the sdr
UE_STARTUP_TIMEOUT: int = RF_MAX_TIMEOUT
GNB_STARTUP_TIMEOUT: int = 5  # GNB delay (we wait x seconds and check it's still alive). UE later and has a big timeout
FIVEGC_STARTUP_TIMEOUT: int = RF_MAX_TIMEOUT
ATTACH_TIMEOUT: int = 120


class StartFailure(Exception):
    """
    Some SUT failed to start
    """


# pylint: disable=too-many-arguments,too-many-locals
def start_and_attach(
    ue_array: Sequence[UEStub],
    gnb: GNBStub,
    fivegc: FiveGCStub,
    ue_startup_timeout: int = UE_STARTUP_TIMEOUT,
    gnb_startup_timeout: int = GNB_STARTUP_TIMEOUT,
    fivegc_startup_timeout: int = FIVEGC_STARTUP_TIMEOUT,
    gnb_pre_cmd: str = "",
    gnb_post_cmd: str = "",
    attach_timeout: int = ATTACH_TIMEOUT,
) -> Dict[UEStub, UEAttachedInfo]:
    """
    Start stubs & wait until attach
    """
    start_network(
        ue_array,
        gnb,
        fivegc,
        gnb_startup_timeout,
        fivegc_startup_timeout,
        gnb_pre_cmd,
        gnb_post_cmd,
    )

    return ue_start_and_attach(
        ue_array,
        gnb,
        fivegc,
        ue_startup_timeout=ue_startup_timeout,
        attach_timeout=attach_timeout,
    )


# pylint: disable=too-many-arguments,too-many-locals
def start_network(
    ue_array: Sequence[UEStub],
    gnb: GNBStub,
    fivegc: FiveGCStub,
    gnb_startup_timeout: int = GNB_STARTUP_TIMEOUT,
    fivegc_startup_timeout: int = FIVEGC_STARTUP_TIMEOUT,
    gnb_pre_cmd: str = "",
    gnb_post_cmd: str = "",
):
    """
    Start Network (5GC + gNB)
    """

    ue_def_for_gnb = UEDefinition()
    for ue_stub in ue_array:
        ue_def: UEDefinition = ue_stub.GetDefinition(Empty())
        fivegc.AddUESubscriber(ue_def.subscriber)
        if ue_def.zmq_ip is not None:
            ue_def_for_gnb = ue_def

    try:
        # 5GC Start
        fivegc.Start(StartInfo(timeout=fivegc_startup_timeout))
        logging.info("5GC [%s] started", id(fivegc))

        # GNB Start
        gnb.Start(
            GNBStartInfo(
                ue_definition=ue_def_for_gnb,
                fivegc_definition=fivegc.GetDefinition(Empty()),
                start_info=StartInfo(
                    timeout=gnb_startup_timeout,
                    pre_commands=gnb_pre_cmd,
                    post_commands=gnb_post_cmd,
                ),
            )
        )
        logging.info("GNB [%s] started", id(gnb))

    except grpc.RpcError as err:
        # pylint: disable=protected-access
        if err._state.code is grpc.StatusCode.ABORTED:
            raise StartFailure from None
        raise err from None


def ue_start_and_attach(
    ue_array: Sequence[UEStub],
    gnb: GNBStub,
    fivegc: FiveGCStub,
    ue_startup_timeout: int = UE_STARTUP_TIMEOUT,
    attach_timeout: int = ATTACH_TIMEOUT,
) -> Dict[UEStub, UEAttachedInfo]:
    """
    Start an array of UEs and wait until attached to already running gnb and 5gc
    """

    try:
        for ue_stub in ue_array:
            ue_stub.Start(
                UEStartInfo(
                    gnb_definition=gnb.GetDefinition(Empty()),
                    fivegc_definition=fivegc.GetDefinition(Empty()),
                    start_info=StartInfo(timeout=ue_startup_timeout),
                )
            )
            logging.info("UE [%s] started", id(ue_stub))

    except grpc.RpcError as err:
        # pylint: disable=protected-access
        if err._state.code is grpc.StatusCode.ABORTED:
            raise StartFailure from None
        raise err from None

    # Attach in parallel
    ue_attach_task_dict: Dict[UEStub, grpc.Future] = {
        ue_stub: ue_stub.WaitUntilAttached.future(UInteger(value=attach_timeout)) for ue_stub in ue_array
    }
    for ue_stub, task in ue_attach_task_dict.items():
        task.add_done_callback(lambda _task, _ue_stub=ue_stub: _log_attached_ue(_task, _ue_stub))

    ue_attach_info_dict: Dict[UEStub, UEAttachedInfo] = {}
    with suppress(grpc.RpcError):
        ue_attach_info_dict = {
            ue_stub: task.result() for ue_stub, task in ue_attach_task_dict.items()  # Waiting for attach
        }

    if not ue_attach_info_dict:
        pytest.fail("Attach timeout reached")

    return ue_attach_info_dict


def _log_attached_ue(future: grpc.Future, ue_stub: UEStub):
    with suppress(grpc.RpcError):
        logging.info(
            "UE [%s] attached: \n%s%s ",
            id(ue_stub),
            ue_stub.GetDefinition(Empty()).subscriber,
            future.result(),
        )


def ping(
    ue_attach_info_dict: Dict[UEStub, UEAttachedInfo],
    fivegc: FiveGCStub,
    ping_count,
):
    """
    Ping command between an UE and a 5GC
    """

    ping_success = True

    # For each attached UE
    for ue_stub, ue_attached_info in ue_attach_info_dict.items():
        # Launch both ping in parallel: ue -> 5gc and 5gc -> ue
        ue_to_fivegc = ue_stub.Ping.future(PingRequest(address=ue_attached_info.ipv4_gateway, count=ping_count))
        fivegc_to_ue = fivegc.Ping.future(PingRequest(address=ue_attached_info.ipv4, count=ping_count))

        # Wait both ping to end
        ue_to_fivegc_result: PingResponse = ue_to_fivegc.result()
        fivegc_to_ue_result: PingResponse = fivegc_to_ue.result()

        # Wait both ping to end & print result
        _print_ping_result(f"[{ue_attached_info.ipv4}] UE -> 5GC", ue_to_fivegc_result)
        _print_ping_result(f"[{ue_attached_info.ipv4}] 5GC -> UE", fivegc_to_ue_result)

        ping_success &= ue_to_fivegc_result.status and fivegc_to_ue_result.status

    if not ping_success:
        pytest.fail("Ping. Some packages got lost.")


def _print_ping_result(msg: str, result: PingResponse):
    log_fn = logging.info
    if not result.status:
        log_fn = logging.error
    log_fn("Ping %s: %s", msg, result)


def iperf(
    ue_attach_info_dict: Dict[UEStub, UEAttachedInfo],
    fivegc: FiveGCStub,
    protocol: IPerfProto,
    direction: IPerfDir,
    iperf_duration: int,
    bitrate: int,
    bitrate_threshold_ratio: float,  # real_bitrate > (bitrate_threshold_ratio * ideal_bitrate)
):
    """
    iperf command between an UE and a 5GC
    """

    iperf_success = True

    # For each attached UE
    for ue_stub, ue_attached_info in ue_attach_info_dict.items():
        # Start IPerf Server
        task, iperf_request = iperf_start(
            ue_stub, ue_attached_info, fivegc, protocol, direction, iperf_duration, bitrate
        )
        iperf_success &= iperf_wait_until_finish(ue_attached_info, fivegc, task, iperf_request, bitrate_threshold_ratio)

    if not iperf_success:
        pytest.fail("iperf did not achieve the expected data rate.")


def iperf_start(
    ue: UEStub,  # pylint: disable=invalid-name
    ue_attached_info: UEAttachedInfo,
    fivegc: FiveGCStub,
    protocol: IPerfProto,
    direction: IPerfDir,
    duration: int,
    bitrate: int,
) -> Tuple[grpc.Future, IPerfRequest]:
    """
    Start a Iperf and keep it running
    """

    iperf_request = IPerfRequest(
        server=fivegc.StartIPerfService(String(value=ue_attached_info.ipv4_gateway)),
        duration=duration,
        direction=direction,
        proto=protocol,
        bitrate=bitrate,
    )

    # Run iperf
    task = ue.IPerf.future(iperf_request)

    logging.info(
        "Iperf %s [%s %s] started",
        ue_attached_info.ipv4,
        _iperf_proto_to_str(iperf_request.proto),
        _iperf_dir_to_str(iperf_request.direction),
    )

    return (task, iperf_request)


def iperf_wait_until_finish(
    ue_attached_info: UEAttachedInfo,
    fivegc: FiveGCStub,
    task: grpc.Future,
    iperf_request: IPerfRequest,
    bitrate_threshold_ratio: float,  # real_bitrate > (bitrate_threshold_ratio * ideal_bitrate)
) -> bool:
    """
    Wait until the requested iperf has finished.
    """

    # Stop server, get results and print it
    with suppress(grpc.RpcError):
        task.result()
    iperf_data: IPerfResponse = fivegc.StopIPerfService(iperf_request.server)
    logging.info(
        "Iperf %s [%s %s] result %s",
        ue_attached_info.ipv4,
        _iperf_proto_to_str(iperf_request.proto),
        _iperf_dir_to_str(iperf_request.direction),
        iperf_data,
    )

    # Assertion
    iperf_success = True
    if iperf_request.direction in (IPerfDir.DOWNLINK, IPerfDir.BIDIRECTIONAL):
        if iperf_data.downlink.bits_per_second == 0:
            logging.warning(
                "Downlink bitrate is 0. Requested: %s",
                iperf_request.bitrate,
            )
        elif iperf_data.downlink.bits_per_second < bitrate_threshold_ratio * iperf_request.bitrate:
            logging.warning(
                "Downlink bitrate too low. Requested: %s - Measured: %s",
                iperf_request.bitrate,
                iperf_data.downlink.bits_per_second,
            )
            iperf_success = False
    if iperf_request.direction in (IPerfDir.UPLINK, IPerfDir.BIDIRECTIONAL):
        if iperf_data.uplink.bits_per_second == 0:
            logging.warning(
                "Uplink bitrate is 0. Requested: %s",
                iperf_request.bitrate,
            )
        elif iperf_data.uplink.bits_per_second < bitrate_threshold_ratio * iperf_request.bitrate:
            logging.warning(
                "Uplink bitrate too low. Requested: %s - Measured: %s",
                iperf_request.bitrate,
                iperf_data.uplink.bits_per_second,
            )
            iperf_success = False
    return iperf_success


def _iperf_proto_to_str(proto):
    return {IPerfProto.TCP: "tcp", IPerfProto.UDP: "udp"}[proto]


def _iperf_dir_to_str(direction):
    return {
        IPerfDir.DOWNLINK: "downlink",
        IPerfDir.UPLINK: "uplink",
        IPerfDir.BIDIRECTIONAL: "bidirectional",
    }[direction]


def stop(
    ue_array: Sequence[UEStub],
    gnb: GNBStub,
    fivegc: FiveGCStub,
    retina_data: RetinaTestData,
    ue_stop_timeout: int = 0,  # Auto
    gnb_stop_timeout: int = 0,
    fivegc_stop_timeout: int = 0,
    log_search: bool = True,
    warning_as_errors: bool = True,
    fail_if_kos: bool = False,
):
    """
    Stop ue(s), gnb and 5gc
    """
    # Stop
    error_msg_array = []
    for index, ue_stub in enumerate(ue_array):
        error_msg_array.append(
            _stop_stub(
                ue_stub,
                f"UE_{index+1}",
                retina_data,
                ue_stop_timeout,
                log_search,
                warning_as_errors,
            )
        )
    error_msg_array.append(_stop_stub(gnb, "GNB", retina_data, gnb_stop_timeout, log_search, warning_as_errors))
    error_msg_array.append(_stop_stub(fivegc, "5GC", retina_data, fivegc_stop_timeout, log_search, warning_as_errors))

    # Fail if stop errors
    error_msg_array = list(filter(bool, error_msg_array))
    if error_msg_array:
        pytest.fail(
            f"Stop stage. {error_msg_array[0]}"
            + (("\nFull list of errors:\n - " + "\n - ".join(error_msg_array)) if len(error_msg_array) > 1 else "")
        )

    # Metrics after Stop
    metrics_msg_array = []
    for index, ue_stub in enumerate(ue_array):
        metrics_msg_array.append(_get_metrics(ue_stub, f"UE_{index+1}", fail_if_kos=fail_if_kos))
    metrics_msg_array.append(_get_metrics(gnb, "GNB", fail_if_kos=fail_if_kos))
    metrics_msg_array.append(_get_metrics(fivegc, "5GC", fail_if_kos=fail_if_kos))

    # Fail if metric errors
    metrics_msg_array = list(filter(bool, metrics_msg_array))
    if metrics_msg_array:
        pytest.fail(
            f"Metrics validation. {metrics_msg_array[0]}"
            + (("\nFull list of errors:\n - " + "\n - ".join(metrics_msg_array)) if len(metrics_msg_array) > 1 else "")
        )


def ue_stop(
    ue_array: Sequence[UEStub],
    retina_data: RetinaTestData,
    ue_stop_timeout: int = 0,  # Auto
    log_search: bool = True,
    warning_as_errors: bool = True,
):
    """
    Stop an array of UEs to detach from already running gnb and 5gc
    """
    error_msg_array = []
    for index, ue_stub in enumerate(ue_array):
        error_msg_array.append(
            _stop_stub(ue_stub, f"UE_{index+1}", retina_data, ue_stop_timeout, log_search, warning_as_errors)
        )
    error_msg_array = list(filter(bool, error_msg_array))
    if error_msg_array:
        pytest.fail(
            f"UE Stop. {error_msg_array[0]}"
            + (("\nFull list of errors:\n - " + "\n - ".join(error_msg_array)) if len(error_msg_array) > 1 else "")
        )


def _stop_stub(
    stub: RanStub,
    name: str,
    retina_data: RetinaTestData,
    timeout: int = 0,
    log_search: bool = True,
    warning_as_errors: bool = True,
) -> str:
    """
    Stop a stub in the defined timeout (0=auto).
    It uses retina_data to save artifacts in case of failure
    """

    error_msg = ""

    with suppress(grpc.RpcError):
        stop_info: StopResponse = stub.Stop(UInteger(value=timeout))

        if stop_info.exit_code:
            retina_data.download_artifacts = True
            error_msg = f"{name} crashed with exit code {stop_info.exit_code}. "

        if log_search:
            log_msg = f"{name} has {stop_info.error_count} errors and {stop_info.warning_count} warnings. "
            if stop_info.error_count:
                log_msg += f"First error is: {stop_info.error_msg}"
            elif stop_info.warning_count:
                log_msg += f"First warning is: {stop_info.warning_msg}"

            if stop_info.error_count or (warning_as_errors and stop_info.warning_count):
                retina_data.download_artifacts = True
                error_msg += log_msg
            elif not warning_as_errors and stop_info.warning_count:
                logging.warning(log_msg)

        if error_msg:
            logging.error(error_msg)
        else:
            logging.info("%s has stopped", name)

    return error_msg


def _get_metrics(stub: RanStub, name: str, fail_if_kos: bool = False) -> str:
    error_msg = ""

    if fail_if_kos:
        with suppress(grpc.RpcError):
            metrics: Metrics = stub.GetMetrics(Empty())

            if metrics.nof_kos or metrics.nof_retx:
                error_msg = f"{name} has:"

            if metrics.nof_kos:
                kos_msg = f" {metrics.nof_kos} KOs"
                error_msg += kos_msg + "."
                logging.error("%s has%s", name, kos_msg)

            if metrics.nof_retx:
                retrx_msg = f" {metrics.nof_retx} retrxs"
                error_msg += retrx_msg + "."
                logging.error("%s has%s", name, retrx_msg)

    return error_msg
