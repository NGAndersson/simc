// ==========================================================================
// Dedmonwakeen's Raid DPS/TPS Simulator.
// Send questions to natehieter@gmail.com
// ==========================================================================

#include "actor_target_data.hpp"
#include "player.hpp"
#include "actor_pair.hpp"
#include "sim/sim.hpp"

actor_target_data_t::actor_target_data_t( player_t* target, player_t* source ) :
  actor_pair_t( target, source ), debuff(), dot()
{
  for (auto & elem : source -> sim -> target_data_initializer)
  {
    elem( this );d
  }
}