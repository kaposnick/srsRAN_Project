/*
 *
 * Copyright 2021-2023 Software Radio Systems Limited
 *
 * This file is part of srsRAN.
 *
 * srsRAN is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsRAN is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include "srsran/e2/e2_du_metrics_manager.h"

using namespace srsran;

e2_du_metrics_manager::e2_du_metrics_manager()
{
  ue_metrics_queue.resize(MAX_UE_METRICS);
}

void e2_du_metrics_manager::report_metrics(span<const scheduler_ue_metrics> ue_metrics)
{
  for (auto& ue_metric : ue_metrics) {
    if (ue_metrics_queue.size() == MAX_UE_METRICS) {
      ue_metrics_queue.pop_front();
    }
    ue_metrics_queue.push_back(ue_metric);
  }
}

void e2_du_metrics_manager::get_metrics(scheduler_ue_metrics& ue_metrics)
{
  ue_metrics = ue_metrics_queue.front();
}