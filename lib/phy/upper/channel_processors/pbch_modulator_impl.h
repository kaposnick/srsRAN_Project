/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2021 Software Radio Systems Limited
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the distribution.
 *
 */
#ifndef SRSGNB_CHANNEL_PROCESSORS_PBCH_MODULATOR_IMPL_H_
#define SRSGNB_CHANNEL_PROCESSORS_PBCH_MODULATOR_IMPL_H_

#include "srsgnb/phy/upper/channel_modulation/modulation_mapper.h"
#include "srsgnb/phy/upper/channel_processors/pbch_modulator.h"
#include "srsgnb/phy/upper/sequence_generators/pseudo_random_generator.h"

namespace srsgnb {

class pbch_modulator_impl : public pbch_modulator
{
private:
  std::unique_ptr<modulation_mapper>       modulator;
  std::unique_ptr<pseudo_random_generator> scrambler;

  /// Implements TS 38.211 section 7.3.3.1 Scrambling
  /// \param b [in] Inputs bits to scramble
  /// \param b_hat [out] Output bits after scrambling
  /// \param args [in] PBCH modulator arguments
  void scramble(span<const uint8_t>& b, std::array<uint8_t, M_bit>& b_hat, const args_t& args);

  /// Implements TS 38.211 section 7.3.3.2 Modulation
  /// \param b_hat [in] Inputs bits to scramble
  /// \param d_pbch [out] Output symbols
  void modulate(const std::array<uint8_t, M_bit>& b_hat, span<cf_t> d_pbch);

  /// Implements TS 38.211 section 7.3.3.3 Mapping to physical resources
  /// \param d_pbch [in] provides the symbols to map
  /// \param grid [in, out] is the destination resource grid
  /// \param args [in] PBCH modulator arguments
  void map(const std::array<cf_t, M_symb>& d_pbch, resource_grid_writer& grid, const args_t& args);

public:
  explicit pbch_modulator_impl();

  ~pbch_modulator_impl() = default;

  void put(span<const uint8_t> bits, resource_grid_writer& grid, const args_t& args) override;
};

} // namespace srsgnb

#endif // SRSGNB_CHANNEL_PROCESSORS_PBCH_MODULATOR_IMPL_H_
