#include "simulationcraft.hpp"

#include "sc_warlock.hpp"

#include "sc_warlock_pets.hpp"
#include "util/util.hpp"

namespace warlock
{
using namespace helpers;

  struct warlock_spell_t : public spell_t
  {
    struct affected_by_t
    {
      // Class

      // Affliction
      bool potent_afflictions_td = false;
      bool potent_afflictions_dd = false;
      bool dread_touch = false;
      bool wrath_of_consumption = false;
      bool haunted_soul  = false;
      bool creeping_death = false;

      // Demonology
      bool master_demonologist_dd = false;
      bool houndmasters = false;
      bool soul_conduit_base_cost = false;

      // Destruction
      bool chaotic_energies = false;
      bool havoc = false;
      bool roaring_blaze = false;
      bool chaos_incarnate = false;
      bool umbrafire_embers_dd = false;
      bool umbrafire_embers_td = false;
    } affected_by;

    struct triggers_t
    {
      // Class

      // Affliction

      // Demonology

      // Destruction
      bool shadow_invocation_direct = false;
      bool shadow_invocation_tick = false;
    } triggers;

    warlock_spell_t( util::string_view token, warlock_t* p, const spell_data_t* s = spell_data_t::nil() )
    : spell_t( token, p, s ),
      affected_by()
    {
      may_crit = true;
      tick_may_crit = true;
      weapon_multiplier = 0.0;

      affected_by.potent_afflictions_td = data().affected_by( p->warlock_base.potent_afflictions->effectN( 1 ) );
      affected_by.potent_afflictions_dd = data().affected_by( p->warlock_base.potent_afflictions->effectN( 2 ) );
      affected_by.dread_touch = data().affected_by( p->talents.dread_touch_debuff->effectN( 1 ) );
      affected_by.wrath_of_consumption = data().affected_by( p->talents.wrath_of_consumption_buff->effectN( 1 ) );
      affected_by.haunted_soul = data().affected_by( p->talents.haunted_soul_buff->effectN( 1 ) );
      affected_by.creeping_death = data().affected_by( p->talents.creeping_death->effectN( 1 ) );

      affected_by.master_demonologist_dd = data().affected_by( p->warlock_base.master_demonologist->effectN( 2 ) );
      affected_by.houndmasters = data().affected_by( p->talents.the_houndmasters_stratagem_debuff->effectN( 1 ) );

      affected_by.roaring_blaze = data().affected_by( p->talents.conflagrate_debuff->effectN( 1 ) );
      affected_by.umbrafire_embers_dd = data().affected_by( p->tier.umbrafire_embers->effectN( 1 ) );
      affected_by.umbrafire_embers_td = data().affected_by( p->tier.umbrafire_embers->effectN( 2 ) );
    }

    warlock_t* p()
    { return static_cast<warlock_t*>( player ); }
    
    const warlock_t* p() const
    { return static_cast<warlock_t*>( player ); }

    warlock_td_t* td( player_t* t )
    { return p()->get_target_data( t ); }

    const warlock_td_t* td( player_t* t ) const
    { return p()->get_target_data( t ); }

    void reset() override
    { spell_t::reset(); }

    void consume_resource() override
    {
      spell_t::consume_resource();

      if ( resource_current == RESOURCE_SOUL_SHARD && p()->in_combat )
      {
        int shards_used = as<int>( cost() );
        int base_shards = as<int>( base_cost() ); // Power Overwhelming is ignoring any cost changes

        if ( p()->talents.soul_conduit->ok() )
        {
          // Soul Conduit events are delayed slightly (100 ms) in sims to avoid instantaneous reactions
          make_event<sc_event_t>( *p()->sim, p(), as<int>( affected_by.soul_conduit_base_cost ? base_shards : last_resource_cost ) );
        }

        if ( p()->buffs.rain_of_chaos->check() && shards_used > 0 )
        {
          for ( int i = 0; i < shards_used; i++ )
          {
            if ( p()->rain_of_chaos_rng->trigger() )
            {
              p()->warlock_pet_list.infernals.spawn( p()->talents.summon_infernal_roc->duration() );
              p()->procs.rain_of_chaos->occur();
            }
          }
        }

        if ( p()->talents.ritual_of_ruin.ok() && shards_used > 0 )
        {
          int overflow = p()->buffs.impending_ruin->check() + shards_used - p()->buffs.impending_ruin->max_stack();
          p()->buffs.impending_ruin->trigger( shards_used ); // Stack change callback switches Impending Ruin to Ritual of Ruin if max stacks reached
          if ( overflow > 0 )
          {
            make_event( sim, 1_ms, [ this, overflow ] { p()->buffs.impending_ruin->trigger( overflow ); } );
          }
        }

        if ( p()->talents.power_overwhelming->ok() && base_shards > 0 )
        {
          p()->buffs.power_overwhelming->trigger( base_shards );
        }

        // 2022-10-17: Spell data is missing the % chance!
        // Need to test further, but chance appears independent of shard cost
        // Also procs even if the cast is free due to other effects
        if ( base_shards > 0 && p()->sets->has_set_bonus( WARLOCK_DESTRUCTION, T29, B2 ) && rng().roll( 0.2 ) )
        {
          p()->buffs.chaos_maelstrom->trigger();
          p()->procs.chaos_maelstrom->occur();
        }
      }
    }

    void execute() override
    {
      spell_t::execute();

      if ( p()->talents.rolling_havoc.ok() && use_havoc() )
      {
        p()->buffs.rolling_havoc->trigger();
      }
    }

    void impact( action_state_t* s ) override
    {
      spell_t::impact( s );

      if ( p()->talents.reverse_entropy.ok() )
      {
        if ( p()->buffs.reverse_entropy->trigger() )
        {
          p()->procs.reverse_entropy->occur();
        }
      }

      if ( affected_by.havoc && p()->talents.mayhem.ok() )
      {
        // Havoc debuff has an ICD, so it is safe to attempt a trigger
        auto tl = target_list();
        auto n = available_targets( tl );

        if ( n > 1u )
        {
          player_t* trigger_target = tl.at( 1u + rng().range( n - 1u ) );
          if ( td( trigger_target )->debuffs_havoc->trigger() )
          {
            p()->procs.mayhem->occur();
          }
        }
      }

      if ( p()->talents.shadow_invocation->ok() && triggers.shadow_invocation_direct && rng().roll( p()->shadow_invocation_proc_chance ) )
      {
        p()->proc_actions.bilescourge_bombers_proc->execute_on_target( s->target );
        p()->procs.shadow_invocation->occur();
      }
    }

    void tick( dot_t* d ) override
    {
      spell_t::tick( d );

      if ( p()->talents.reverse_entropy.ok() )
      {
        if ( p()->buffs.reverse_entropy->trigger() )
        {
          p()->procs.reverse_entropy->occur();
        }
      }

      if ( p()->talents.shadow_invocation.ok() && triggers.shadow_invocation_tick && rng().roll( p()->shadow_invocation_proc_chance ) )
      {
        p()->proc_actions.bilescourge_bombers_proc->execute_on_target( d->target );
        p()->procs.shadow_invocation->occur();
      }
    }

    double composite_target_multiplier( player_t* t ) const override
    {
      double m = spell_t::composite_target_multiplier( t );

      if ( p()->talents.dread_touch.ok() && affected_by.dread_touch && td( t )->debuffs_dread_touch->check() )
      {
        m *= 1.0 + td( t )->debuffs_dread_touch->check_stack_value();
      }

      if ( p()->talents.the_houndmasters_stratagem.ok() && affected_by.houndmasters )
      {
        m *= 1.0 + td( t )->debuffs_the_houndmasters_stratagem->check_value();
      }

      if ( p()->talents.roaring_blaze.ok() && affected_by.roaring_blaze && td( t )->debuffs_conflagrate->check() )
      {
        m *= 1.0 + td( t )->debuffs_conflagrate->check_value();
      }

      if ( p()->sets->has_set_bonus( WARLOCK_DESTRUCTION, T30, B4 )
        && ( affected_by.umbrafire_embers_dd || affected_by.umbrafire_embers_td )
        && p()->buffs.umbrafire_embers->check() )
      {
        m *= 1.0 + p()->buffs.umbrafire_embers->check_stack_value();
      }

      return m;
    }

    double action_multiplier() const override
    {
      double m = spell_t::action_multiplier();

      m *= 1.0 + p()->buffs.demonic_synergy->check_stack_value();

      if ( demonology() && affected_by.master_demonologist_dd )
      {
        m *= 1.0 + p()->cache.mastery_value();
      }

      if ( destruction() && affected_by.chaotic_energies )
      {
        double destro_mastery_value = p()->cache.mastery_value() / 2.0;
        double chaotic_energies_rng = rng().range( 0, destro_mastery_value );

        if ( affected_by.chaos_incarnate )
          chaotic_energies_rng = destro_mastery_value;

        m *= 1.0 + chaotic_energies_rng + destro_mastery_value;
      }
      
      return m;
    }

    double composite_da_multiplier( const action_state_t* s ) const override
    {
      double m = spell_t::composite_da_multiplier( s );

      if ( affliction() && affected_by.potent_afflictions_dd )
      {
        m *= 1.0 + p()->cache.mastery_value();
      }

      return m;
    }

    double composite_ta_multiplier( const action_state_t* s ) const override
    {
      double m = spell_t::composite_ta_multiplier( s );

      if ( affliction() && affected_by.potent_afflictions_td )
      {
        m *= 1.0 + p()->cache.mastery_value();
      }

      if ( p()->talents.wrath_of_consumption.ok() && affected_by.wrath_of_consumption && p()->buffs.wrath_of_consumption->check() )
      {
        m *= 1.0 + p()->buffs.wrath_of_consumption->check_stack_value();
      }

      if ( p()->talents.haunted_soul.ok() && affected_by.haunted_soul && p()->buffs.haunted_soul->check() )
      {
        m *= 1.0 + p()->buffs.haunted_soul->check_stack_value();
      }

      return m;
    }

    double composite_crit_damage_bonus_multiplier() const override
    {
      double m = spell_t::composite_crit_damage_bonus_multiplier();

      if ( p()->sets->has_set_bonus( WARLOCK_DESTRUCTION, T29, B4 ) )
      {
        m += p()->sets->set( WARLOCK_DESTRUCTION, T29, B4 )->effectN( 1 ).percent();
      }

      return m;
    }

    void extend_dot( dot_t* dot, timespan_t extend_duration )
    {
      if ( dot->is_ticking() )
      {
        // TOCHECK: Do we always cap out at pandemic amount (+50%)?
        dot->adjust_duration( extend_duration, dot->current_action->dot_duration * 1.5 );
      }
    }

    bool use_havoc() const
    {
      // Ensure we do not try to hit the same target twice.
      return affected_by.havoc && p()->havoc_target && p()->havoc_target != target;
    }

    // We need to ensure that the target cache is invalidated, which sometimes does not take 
    // place in select_target() due to other methods we have overridden involving Havoc
    bool select_target() override
    {
      auto saved_target = target;

      bool passed = spell_t::select_target();

      if ( passed && target != saved_target && use_havoc() )
        target_cache.is_valid = false;

      return passed;
    }

    int n_targets() const override
    {
      if ( destruction() && use_havoc() )
      {
        assert( spell_t::n_targets() == 0 );
        return 2;
      }
      else
      {
        return spell_t::n_targets();
      }
    }

    size_t available_targets( std::vector<player_t*>& tl ) const override
    {
      spell_t::available_targets( tl );

      // Check target list size to prevent some silly scenarios where Havoc target
      // is the only target in the list.
      if ( destruction() && tl.size() > 1 && use_havoc())
      {
        // We need to make sure that the Havoc target ends up second in the target list,
        // so that Havoc spells can pick it up correctly.
        auto it = range::find(tl, p()->havoc_target);
        if (it != tl.end())
        {
          tl.erase(it);
          tl.insert(tl.begin() + 1, p()->havoc_target);
        }
      }

      return tl.size();
    }

    void init() override
    {
      spell_t::init();

      if ( destruction() && affected_by.havoc )
      {
        base_aoe_multiplier *= p()->talents.havoc_debuff->effectN( 1 ).percent();
        p()->havoc_spells.push_back( this );
      }

      if ( p()->talents.creeping_death.ok() && affected_by.creeping_death )
      {
        base_tick_time *= 1.0 + p()->talents.creeping_death->effectN( 1 ).percent();
      }
    }

    bool affliction() const
    { return p()->specialization() == WARLOCK_AFFLICTION; }

    bool demonology() const
    { return p()->specialization() == WARLOCK_DEMONOLOGY; }

    bool destruction() const
    { return p()->specialization() == WARLOCK_DESTRUCTION; }
  };

  // Shared Class Actions Begin

  struct summon_pet_t : public warlock_spell_t
  {
    timespan_t summoning_duration;
    std::string pet_name;
    warlock_pet_t* pet;

  private:
    void _init_summon_pet_t()
    {
      util::tokenize( pet_name );
      harmful = false;

      if ( data().ok()
        && std::find( p()->pet_name_list.begin(), p()->pet_name_list.end(), pet_name ) == p()->pet_name_list.end() )
        p()->pet_name_list.push_back( pet_name );

      target = player;
    }

  public:
    summon_pet_t( util::string_view n, warlock_t* p, const spell_data_t* sd )
      : warlock_spell_t( n, p, sd ),
      summoning_duration( 0_ms ),
      pet_name( n ),
      pet( nullptr )
    { _init_summon_pet_t(); }

    summon_pet_t( util::string_view n, warlock_t* p, int id )
      : summon_pet_t( n, p, p->find_spell( id ) )
    { }

    summon_pet_t( util::string_view n, warlock_t* p )
      : summon_pet_t( n, p, p->find_class_spell( n ) )
    { }

    void init_finished() override
    {
      pet = debug_cast<warlock_pet_t*>( player->find_pet( pet_name ) );

      warlock_spell_t::init_finished();
    }

    virtual void execute() override
    {
      pet->summon( summoning_duration );

      warlock_spell_t::execute();
    }

    bool ready() override
    {
      if ( !pet )
        return false;

      return warlock_spell_t::ready();
    }
  };

  struct summon_main_pet_t : public summon_pet_t
  {
    summon_main_pet_t( util::string_view n, warlock_t* p, int id )
      : summon_pet_t( n, p, id )
    { ignore_false_positive = true; }

    summon_main_pet_t( util::string_view n, warlock_t* p )
      : summon_pet_t( n, p )
    { ignore_false_positive = true; }

    void schedule_execute( action_state_t* s = nullptr ) override
    {
      summon_pet_t::schedule_execute( s );

      if ( p()->warlock_pet_list.active )
      {
        p()->warlock_pet_list.active->dismiss();
        p()->warlock_pet_list.active = nullptr;
      }
    }

    virtual bool ready() override
    {
      if ( p()->warlock_pet_list.active == pet )
        return false;

      return summon_pet_t::ready();
    }

    virtual void execute() override
    {
      summon_pet_t::execute();

      p()->warlock_pet_list.active = pet;

      if ( p()->buffs.grimoire_of_sacrifice->check() )
        p()->buffs.grimoire_of_sacrifice->expire();
    }
  };

  struct drain_life_t : public warlock_spell_t
  {

    // Note: Soul Rot (Affliction talent) turns Drain Life into a multi-target channeled spell. Nothing else in simc behaves this way and
    // we currently do not have core support for it. Applying this dot to the secondary targets should cover most of the behavior, although
    // it will be unable to handle the case where primary channel target dies (in-game, this appears to force-swap primary target to another
    // target currently affected by Drain Life if possible).
    struct drain_life_dot_t : public warlock_spell_t
    {
      drain_life_dot_t( warlock_t* p ) : warlock_spell_t( "Drain Life (AoE)", p, p->warlock_base.drain_life )
      {
        dual = background = true;

        dot_duration *= 1.0 + p->talents.grim_feast->effectN( 1 ).percent();
        base_tick_time *= 1.0 + p->talents.grim_feast->effectN( 2 ).percent();
      }

      double cost_per_tick( resource_e ) const override
      { return 0.0; }

      double action_multiplier() const override
      {
        double m = warlock_spell_t::action_multiplier();

        if ( p()->talents.inevitable_demise.ok() && p()->buffs.inevitable_demise->check() )
        {
          m *= 1.0 + p()->buffs.inevitable_demise->check_stack_value();
        }

        return m;
      }

      timespan_t composite_dot_duration( const action_state_t* s ) const override
      {
        // Model this like channel behavior - we can't actually set it as a channel without breaking things
        return dot_duration * ( tick_time( s ) / base_tick_time );
      }
    };
    
    drain_life_dot_t* aoe_dot;

    drain_life_t( warlock_t* p, util::string_view options_str ) : warlock_spell_t( "Drain Life", p, p->warlock_base.drain_life )
    {
      parse_options( options_str );

      aoe_dot = new drain_life_dot_t( p );
      add_child( aoe_dot );

      channeled = true;

      dot_duration *= 1.0 + p->talents.grim_feast->effectN( 1 ).percent();
      base_tick_time *= 1.0 + p->talents.grim_feast->effectN( 2 ).percent();

      triggers.shadow_invocation_tick = true;
    }

    void execute() override
    {
      if ( p()->talents.inevitable_demise.ok() && p()->buffs.inevitable_demise->check() > 0 )
      {
        if ( p()->buffs.drain_life->check() )
        {
          p()->buffs.inevitable_demise->expire();
        }
      }

      warlock_spell_t::execute();

      p()->buffs.drain_life->trigger();

      if ( p()->talents.soul_rot.ok() && p()->buffs.soul_rot->check() )
      {
        const auto& tl = target_list();

        for ( auto& t : tl )
        {
          // Don't apply AoE version to primary target
          if ( t == target )
            continue;

          if ( td( t )->dots_soul_rot->is_ticking() )
          {
            aoe_dot->execute_on_target( t );
          }
        }
      }

      p()->buffs.soulburn->expire();
    }

    double cost_per_tick( resource_e r ) const override
    {
      if ( r == RESOURCE_MANA && p()->buffs.soul_rot->check() )
        return 0.0;

      auto c = warlock_spell_t::cost_per_tick( r );

      if ( r == RESOURCE_MANA && p()->buffs.inevitable_demise->check() )
      {
        c *= 1.0 + p()->talents.inevitable_demise_buff->effectN( 3 ).percent() * p()->buffs.inevitable_demise->check();
      }

      return c;
    }

    double action_multiplier() const override
    {
      double m = warlock_spell_t::action_multiplier();

      if ( p()->talents.inevitable_demise.ok() && p()->buffs.inevitable_demise->check() )
      {
        m *= 1.0 + p()->buffs.inevitable_demise->check_stack_value();
      }

      return m;
    }

    void last_tick( dot_t* d ) override
    {
      p()->buffs.drain_life->expire();
      p()->buffs.inevitable_demise->expire( 1_ms ); // Slight delay added so that the AoE version of Drain Life picks up the benefit of Inevitable Demise

      bool early_cancel = d->remains() > 0_ms;

      warlock_spell_t::last_tick( d );

      // If this is the end of the channel, the AoE DoTs will expire correctly
      // Otherwise, we need to cancel them on the spot
      if ( p()->talents.soul_rot->ok() && early_cancel )
      {
        const auto& tl = target_list();

        for ( auto& t : tl )
        {
          auto data = td( t );
          if ( data->dots_drain_life_aoe->is_ticking() )
            data->dots_drain_life_aoe->cancel();
        }
      }
    }
  };

  struct corruption_t : public warlock_spell_t
  {
    struct corruption_dot_t : public warlock_spell_t
    {
      corruption_dot_t( warlock_t* p ) : warlock_spell_t( "Corruption", p, p->warlock_base.corruption->effectN( 1 ).trigger() )
      {
        tick_zero = false;
        background = dual = true;

        if ( p->talents.absolute_corruption.ok() )
        {
          dot_duration = sim->expected_iteration_time > 0_ms
            ? 2 * sim->expected_iteration_time
            : 2 * sim->max_time * ( 1.0 + sim->vary_combat_length ); // "Infinite" duration
          base_td_multiplier *= 1.0 + p->talents.absolute_corruption->effectN( 2 ).percent(); // 2022-09-25: Only tick damage is affected
        }

        triggers.shadow_invocation_tick = true;
      }

      void tick( dot_t* d ) override
      {
        warlock_spell_t::tick( d );

        if ( result_is_hit( d->state->result ) )
        {
          if ( p()->talents.nightfall->ok() )
          {
            // Blizzard did not publicly release how nightfall was changed.
            // We determined this is the probable functionality copied from Agony by first confirming the
            // DR formula was the same and then confirming that you can get procs on 1st tick.
            // The procs also have a regularity that suggest it does not use a proc chance or rppm.
            // Last checked 09-28-2020.
            double increment_max = 0.13;

            double active_corruptions = p()->get_active_dots( d );
            increment_max *= std::pow( active_corruptions, -2.0 / 3.0 );

            p()->corruption_accumulator += rng().range( 0.0, increment_max );

            if ( p()->corruption_accumulator >= 1 )
            {
              p()->procs.nightfall->occur();
              p()->buffs.nightfall->trigger();
              p()->corruption_accumulator -= 1.0;
            }
          }
        }
      }

      double composite_ta_multiplier( const action_state_t* s ) const override
      {
        double m = warlock_spell_t::composite_ta_multiplier( s );

        if ( p()->talents.sacrolashs_dark_strike.ok() )
        {
          m *= 1.0 + p()->talents.sacrolashs_dark_strike->effectN( 1 ).percent();
        }

        return m;
      }
    };

    corruption_dot_t* periodic;

    corruption_t( warlock_t* p, util::string_view options_str, bool seed_action )
      : warlock_spell_t( "Corruption (Direct)", p, p->warlock_base.corruption )
    {
      parse_options( options_str );

      periodic = new corruption_dot_t( p );
      impact_action = periodic;
      add_child( periodic );

      spell_power_mod.direct = 0; // By default, Corruption does not deal instant damage

      if ( !seed_action && p->warlock_base.xavian_teachings->ok() )
      {
        spell_power_mod.direct = data().effectN( 3 ).sp_coeff();
        base_execute_time *= 1.0 + p->warlock_base.xavian_teachings->effectN( 1 ).percent();
      }
    }

    void impact( action_state_t* s ) override
    {
      bool pi_trigger = p()->talents.pandemic_invocation->ok() && td( s->target )->dots_corruption->is_ticking()
        && td( s->target )->dots_corruption->remains() < p()->talents.pandemic_invocation->effectN( 1 ).time_value();

      warlock_spell_t::impact( s );

      if ( pi_trigger )
        p()->proc_actions.pandemic_invocation_proc->execute_on_target( s->target );
    }

    dot_t* get_dot( player_t* t ) override
    { return periodic->get_dot( t ); }
  };

  struct shadow_bolt_t : public warlock_spell_t
  {
    shadow_bolt_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Shadow Bolt", p, p->talents.drain_soul_dot->ok() ? spell_data_t::not_found() : p->warlock_base.shadow_bolt )
    {
      parse_options( options_str );

      triggers.shadow_invocation_direct = true;

      if ( demonology() )
      {
        energize_type = action_energize::ON_CAST;
        energize_resource = RESOURCE_SOUL_SHARD;
        energize_amount = 1.0;
      }
    }

    timespan_t execute_time() const override
    {
      if ( p()->buffs.nightfall->check() )
      {
        return 0_ms;
      }

      return warlock_spell_t::execute_time();
    }

    void execute() override
    {
      warlock_spell_t::execute();

      if ( time_to_execute == 0_ms )
        p()->buffs.nightfall->decrement();

      if ( p()->talents.demonic_calling->ok() )
        p()->buffs.demonic_calling->trigger();

      p()->buffs.stolen_power_final->expire();
    }

    void impact( action_state_t* s ) override
    {
      warlock_spell_t::impact( s );

      if ( result_is_hit( s->result ) )
      {
        if ( p()->talents.shadow_embrace->ok() )
          td( s->target )->debuffs_shadow_embrace->trigger();

        if ( p()->talents.tormented_crescendo->ok() )
        {
          if ( crescendo_check( p() ) && rng().roll( p()->talents.tormented_crescendo->effectN( 1 ).percent() ) )
          {
            p()->procs.tormented_crescendo->occur();
            p()->buffs.tormented_crescendo->trigger();
          }
        }
      }
    }

    double action_multiplier() const override
    {
      double m = warlock_spell_t::action_multiplier();

      if ( time_to_execute == 0_ms && p()->buffs.nightfall->check() )
      {
        m *= 1.0 + p()->talents.nightfall_buff->effectN( 2 ).percent();
      }

      if ( p()->talents.sacrificed_souls.ok() )
      {
        m *= 1.0 + p()->talents.sacrificed_souls->effectN( 1 ).percent() * p()->active_demon_count();
      }

      if ( p()->talents.stolen_power.ok() && p()->buffs.stolen_power_final->check() )
      {
        m *= 1.0 + p()->talents.stolen_power_final_buff->effectN( 1 ).percent();
      }

      return m;
    }

    double composite_target_multiplier( player_t* t ) const override
    {
      double m = warlock_spell_t::composite_target_multiplier( t );

      if ( p()->talents.withering_bolt.ok() )
      {
        m *= 1.0 + p()->talents.withering_bolt->effectN( 1 ).percent() * std::min( (int)( p()->talents.withering_bolt->effectN( 2 ).base_value() ), p()->get_target_data( t )->count_affliction_dots() );
      }

      return m;
    }
  };

  struct grimoire_of_sacrifice_t : public warlock_spell_t
  {
    grimoire_of_sacrifice_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Grimoire of Sacrifice", p, p->talents.grimoire_of_sacrifice )
    {
      parse_options( options_str );
      harmful = false;
      ignore_false_positive = true;
      target = player;
    }

    bool ready() override
    {
      if ( !p()->warlock_pet_list.active )
        return false;

      return warlock_spell_t::ready();
    }

    void execute() override
    {
      warlock_spell_t::execute();

      if ( p()->warlock_pet_list.active )
      {
        p()->warlock_pet_list.active->dismiss();
        p()->warlock_pet_list.active = nullptr;
        p()->buffs.grimoire_of_sacrifice->trigger();
      }
    }
  };

  struct grimoire_of_sacrifice_damage_t : public warlock_spell_t
  {
    grimoire_of_sacrifice_damage_t( warlock_t* p )
      : warlock_spell_t( "Grimoire of Sacrifice (Proc)", p, p->talents.grimoire_of_sacrifice_proc )
    {
      background = true;
      proc = true;

      base_dd_multiplier *= 1.0 + p->talents.demonic_inspiration->effectN( 2 ).percent();
      base_dd_multiplier *= 1.0 + p->talents.wrathful_minion->effectN( 2 ).percent();
    }
  };

  struct soulburn_t : public warlock_spell_t
  {
    soulburn_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Soulburn", p, p->talents.soulburn )
    {
      parse_options( options_str );
      harmful = false;
      may_crit = false;
    }

    bool ready() override
    {
      if ( p()->buffs.soulburn->check() )
        return false;

      return warlock_spell_t::ready();
    }

    void execute() override
    {
      warlock_spell_t::execute();

      p()->buffs.soulburn->trigger();
    }
  };

  // Catchall action to trigger pet interrupt abilities via main APL.
  struct interrupt_t : public spell_t
  {
    interrupt_t( util::string_view n, warlock_t* p, util::string_view options_str ) :
      spell_t( n, p )
    {
      parse_options( options_str );
      callbacks = true;
      dual = usable_while_casting = true;
      may_miss = may_block = may_crit = false;
      ignore_false_positive = is_interrupt = true;
      trigger_gcd = 0_ms;
    }

    void execute() override
    {
      auto* w = debug_cast<warlock_t*>( player );

      auto pet = w->warlock_pet_list.active;

      switch( pet->pet_type )
      {
        case PET_FELGUARD:
        case PET_FELHUNTER:
          pet->special_action->execute_on_target( target );
          break;
        default:
          break;
      }

      spell_t::execute();
    }

    bool ready() override
    {
      auto* w = debug_cast<warlock_t*>( player );

      if ( !w->warlock_pet_list.active || w->warlock_pet_list.active->is_sleeping() )
        return false;

      auto pet = w->warlock_pet_list.active;

      switch( pet->pet_type )
      {
        case PET_FELGUARD:
        case PET_FELHUNTER:
          if ( !pet->special_action || !pet->special_action->cooldown->up() || !pet->special_action->ready() )
            return false;

          return spell_t::ready();
        default:
          return false;
      }
    }

    bool target_ready( player_t* candidate_target ) override
    {
      if ( !candidate_target->debuffs.casting || !candidate_target->debuffs.casting->check() )
        return false;

      return spell_t::target_ready( candidate_target );
    }
  };

  // Shared Class Actions End
  // Affliction Actions Begin

  struct malefic_rapture_t : public warlock_spell_t
  {
    struct malefic_rapture_damage_t : public warlock_spell_t
    {
      timespan_t t31_soulstealer_extend;

      malefic_rapture_damage_t( warlock_t* p )
        : warlock_spell_t ( "Malefic Rapture (hit)", p, p->talents.malefic_rapture_dmg ),
        t31_soulstealer_extend( p->sets->set( WARLOCK_AFFLICTION, T31, B4 )->effectN( 1 ).time_value() )
      {
        background = dual = true;
        spell_power_mod.direct = p->talents.malefic_rapture->effectN( 1 ).sp_coeff();
        callbacks = false; // Individual hits have been observed to not proc trinkets like Psyche Shredder
      }

      double composite_da_multiplier( const action_state_t* s ) const override
      {
        double m = warlock_spell_t::composite_da_multiplier( s );

        m *= td( s->target )->count_affliction_dots();

        m *= 1.0 + p()->buffs.cruel_epiphany->check_value();

        if ( p()->talents.focused_malignancy.ok() && td( s->target )->dots_unstable_affliction->is_ticking() )
          m *= 1.0 + p()->talents.focused_malignancy->effectN( 1 ).percent();

        if ( p()->buffs.umbrafire_kindling->check() )
          m *= 1.0 + p()->tier.umbrafire_kindling->effectN( 1 ).percent();

        return m;
      }

      void impact( action_state_t* s ) override
      {
        warlock_spell_t::impact( s );

        auto target_data = td( s->target );

        if ( p()->talents.dread_touch.ok() )
        {
          if ( target_data->dots_unstable_affliction->is_ticking() )
            target_data->debuffs_dread_touch->trigger();
        }

        if ( p()->buffs.umbrafire_kindling->check() )
        {
          if ( target_data->dots_agony->is_ticking() )
            target_data->dots_agony->adjust_duration( t31_soulstealer_extend );

          if ( target_data->dots_corruption->is_ticking() )
            target_data->dots_corruption->adjust_duration( t31_soulstealer_extend );

          if ( target_data->dots_siphon_life->is_ticking() )
            target_data->dots_siphon_life->adjust_duration( t31_soulstealer_extend );

          if ( target_data->dots_unstable_affliction->is_ticking() )
            target_data->dots_unstable_affliction->adjust_duration( t31_soulstealer_extend );

          if ( target_data->debuffs_haunt->up() )
            target_data->debuffs_haunt->extend_duration( p(), t31_soulstealer_extend );

          if ( target_data->dots_vile_taint->is_ticking() )
            target_data->dots_vile_taint->adjust_duration( t31_soulstealer_extend );

          if ( target_data->dots_phantom_singularity->is_ticking() )
            target_data->dots_phantom_singularity->adjust_duration( t31_soulstealer_extend );

          if ( target_data->dots_soul_rot->is_ticking() )
            target_data->dots_soul_rot->adjust_duration( t31_soulstealer_extend );
        }
      }

      void execute() override
      {
        int d = td( target )->count_affliction_dots() - 1;
        assert( d < as<int>( p()->procs.malefic_rapture.size() ) && "The procs.malefic_rapture array needs to be expanded." );

        if ( d >= 0 && d < as<int>( p()->procs.malefic_rapture.size() ) )
        {
          p()->procs.malefic_rapture[ d ]->occur();
        }

        warlock_spell_t::execute();

        if ( p()->buffs.umbrafire_kindling->check() )
        {
          p()->buffs.soul_rot->extend_duration( p(), t31_soulstealer_extend );
        }
      }
    };

    malefic_rapture_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Malefic Rapture", p, p->talents.malefic_rapture )
    {
      parse_options( options_str );
      aoe = -1;

      impact_action = new malefic_rapture_damage_t( p );
      add_child( impact_action );
    }

    double cost_pct_multiplier() const override
    {
      double c= warlock_spell_t::cost_pct_multiplier();

      if ( p()->buffs.tormented_crescendo->check() )
      {
        c *= 1.0 + p()->talents.tormented_crescendo_buff->effectN( 3 ).percent();
      }

      return c;
    }

    timespan_t execute_time() const override
    {
      timespan_t t = warlock_spell_t::execute_time();

      if ( p()->buffs.tormented_crescendo->check() )
      {
        t *= 1.0 + p()->talents.tormented_crescendo_buff->effectN( 2 ).percent();
      }

      return t;
    }

    bool ready() override
    {
      if ( !warlock_spell_t::ready() )
        return false;

      target_cache.is_valid = false;
      return target_list().size() > 0;
    }

    void execute() override
    {
      warlock_spell_t::execute();

      p()->buffs.tormented_crescendo->decrement();
      p()->buffs.cruel_epiphany->decrement();
      p()->buffs.umbrafire_kindling->decrement();
    }

    size_t available_targets( std::vector<player_t*>& tl )
    {
      warlock_spell_t::available_targets( tl );

      range::erase_remove( tl, [ this ]( player_t* t ){ return td( t )->count_affliction_dots() == 0; } );

      return tl.size();
    }
  };

  struct unstable_affliction_t : public warlock_spell_t
  {
    unstable_affliction_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Unstable Affliction", p, p->talents.unstable_affliction )
    {
      parse_options( options_str );

      dot_duration += p->talents.unstable_affliction_3->effectN( 1 ).time_value();
    }

    unstable_affliction_t( warlock_t* p )
      : warlock_spell_t( "Unstable Affliction", p , p->talents.soul_swap_ua )
    {
      dot_duration += p->talents.unstable_affliction_3->effectN( 1 ).time_value();
    }

    void execute() override
    {
      if ( p()->ua_target && p()->ua_target != target )
      {
        td( p()->ua_target )->dots_unstable_affliction->cancel();
      }

      p()->ua_target = target;

      warlock_spell_t::execute();
    }

    void impact( action_state_t* s ) override
    {
      bool pi_trigger = p()->talents.pandemic_invocation->ok() && td( s->target )->dots_unstable_affliction->is_ticking()
        && td( s->target )->dots_unstable_affliction->remains() < p()->talents.pandemic_invocation->effectN( 1 ).time_value();

      warlock_spell_t::impact( s );

      if ( pi_trigger )
        p()->proc_actions.pandemic_invocation_proc->execute_on_target( s->target );
    }

    void last_tick( dot_t* d ) override
    {
      warlock_spell_t::last_tick( d );

      p()->ua_target = nullptr;
    }
  };

  struct agony_t : public warlock_spell_t
  {
    agony_t( warlock_t* p, util::string_view options_str ) : warlock_spell_t( "Agony", p, p->warlock_base.agony )
    {
      parse_options( options_str );
      may_crit = false;

      dot_max_stack = as<int>( data().max_stacks() + p->warlock_base.agony_2->effectN( 1 ).base_value() );
      dot_max_stack += as<int>( p->talents.writhe_in_agony->effectN( 1 ).base_value() ); // TOCHECK: Moved this from init(), is this ok?
    }

    void last_tick ( dot_t* d ) override
    {
      if ( p()->get_active_dots( d ) == 1 )
      {
        p()->agony_accumulator = rng().range( 0.0, 0.99 );
      }

      warlock_spell_t::last_tick( d );
    }

    void execute() override
    {
      warlock_spell_t::execute();

      if ( p()->talents.writhe_in_agony.ok() )
      {
        int delta = (int)( p()->talents.writhe_in_agony->effectN( 3 ).base_value() ) - td( execute_state->target )->dots_agony->current_stack();

        if ( delta > 0 )
          td( execute_state->target )->dots_agony->increment( delta );
      }
    }

    void impact( action_state_t* s ) override
    {
      bool pi_trigger = p()->talents.pandemic_invocation->ok() && td( s->target )->dots_agony->is_ticking()
        && td( s->target )->dots_agony->remains() < p()->talents.pandemic_invocation->effectN( 1 ).time_value();

      warlock_spell_t::impact( s );

      if ( pi_trigger )
        p()->proc_actions.pandemic_invocation_proc->execute_on_target( s->target );
    }

    void tick( dot_t* d ) override
    {
      // Blizzard has not publicly released the formula for Agony's chance to generate a Soul Shard.
      // This set of code is based on results from 500+ Soul Shard sample sizes, and matches in-game
      // results to within 0.1% of accuracy in all tests conducted on all targets numbers up to 8.
      // Accurate as of 08-24-2018. TOCHECK regularly. If any changes are made to this section of
      // code, please also update the Time_to_Shard expression in sc_warlock.cpp.
      double increment_max = 0.368;

      double active_agonies = p()->get_active_dots( d );
      increment_max *= std::pow( active_agonies, -2.0 / 3.0 );

      // 2023-09-01: Recent test noted that Creeping Death is once again renormalizing shard generation to be neutral with/without the talent.
      if ( p()->talents.creeping_death->ok() )
        increment_max *= 1.0 + p()->talents.creeping_death->effectN( 1 ).percent();

      p()->agony_accumulator += rng().range( 0.0, increment_max );

      if ( p()->agony_accumulator >= 1 )
      {
        p()->resource_gain( RESOURCE_SOUL_SHARD, 1.0, p()->gains.agony );
        p()->agony_accumulator -= 1.0;

        if ( p()->sets->has_set_bonus( WARLOCK_AFFLICTION, T29, B2 ) && rng().roll( 0.3 ) )
        {
          p()->buffs.cruel_inspiration->trigger();
          p()->procs.cruel_inspiration->occur();

          if ( p()->sets->has_set_bonus( WARLOCK_AFFLICTION, T29, B4 ) )
            p()->buffs.cruel_epiphany->trigger( 2 );
        }
      }

      if ( result_is_hit( d->state->result ) && p()->talents.inevitable_demise.ok() && !p()->buffs.drain_life->check() )
      {
        p()->buffs.inevitable_demise->trigger();
      }

      warlock_spell_t::tick( d );

      td( d->state->target )->dots_agony->increment( 1 );
    }
  };

  struct doom_blossom_t : public warlock_spell_t
  {
    doom_blossom_t( warlock_t* p )
      : warlock_spell_t( "Doom Blossom", p, p->talents.doom_blossom_proc )
    {
      background = dual = true;
      aoe = -1;
    }
  };

  struct seed_of_corruption_t : public warlock_spell_t
  {
    struct seed_of_corruption_aoe_t : public warlock_spell_t
    {
      corruption_t* corr;
      doom_blossom_t* doom_blossom;
      bool cruel_epiphany;
      bool umbrafire_kindling;

      seed_of_corruption_aoe_t( warlock_t* p )
        : warlock_spell_t( "Seed of Corruption (AoE)", p, p->talents.seed_of_corruption_aoe ),
        corr( new corruption_t( p, "", true ) ),
        cruel_epiphany( false ),
        umbrafire_kindling( false ),
        doom_blossom( new doom_blossom_t( p ) )
      {
        aoe = -1;
        background = dual = true;

        corr->background = true;
        corr->dual = true;
        corr->base_costs[ RESOURCE_MANA ] = 0;

        add_child( doom_blossom );
      }

      void impact( action_state_t* s ) override
      {
        warlock_spell_t::impact( s );

        if ( result_is_hit( s->result ) )
        {
          auto tdata = td( s->target );

          if ( tdata->dots_seed_of_corruption->is_ticking() && tdata->soc_threshold > 0 )
          {
            tdata->soc_threshold = 0;
            tdata->dots_seed_of_corruption->cancel();
          }

          if ( p()->talents.doom_blossom.ok() && tdata->dots_unstable_affliction->is_ticking() )
          {
            doom_blossom->execute_on_target( s->target );
            p()->procs.doom_blossom->occur();
          }

          // 2022-09-24 Agonizing Corruption does not apply Agony, only increments existing ones
          if ( p()->talents.agonizing_corruption.ok() && tdata->dots_agony->is_ticking() )
          {
            tdata->dots_agony->increment( (int)( p()->talents.agonizing_corruption->effectN( 1 ).base_value() ) ); 
          }
          
          corr->execute_on_target( s->target );
        }
      }

      double composite_da_multiplier( const action_state_t* s ) const override
      {
        double m = warlock_spell_t::composite_da_multiplier( s );

        if ( p()->buffs.cruel_epiphany->check() && cruel_epiphany )
        {
          m *= 1.0 + p()->buffs.cruel_epiphany->check_value();
        }

        if ( umbrafire_kindling )
        {
          m *= 1.0 + p()->tier.umbrafire_kindling->effectN( 2 ).percent();
        }

        return m;
      }
    };

    seed_of_corruption_aoe_t* explosion;
    seed_of_corruption_aoe_t* epiphany_explosion;
    seed_of_corruption_aoe_t* umbrafire_explosion;

    seed_of_corruption_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Seed of Corruption", p, p->talents.seed_of_corruption ),
      explosion( new seed_of_corruption_aoe_t( p ) ),
      epiphany_explosion( new seed_of_corruption_aoe_t( p ) ),
      umbrafire_explosion( new seed_of_corruption_aoe_t( p ) )
    {
      parse_options( options_str );
      may_crit = false;
      tick_zero = false;
      base_tick_time = dot_duration;
      hasted_ticks = false;

      add_child( explosion );

      epiphany_explosion->cruel_epiphany = true;
      add_child( epiphany_explosion );

      umbrafire_explosion->umbrafire_kindling = true;
      add_child( umbrafire_explosion );

      if ( p->talents.sow_the_seeds.ok() )
      {
        aoe = 1 + as<int>( p->talents.sow_the_seeds->effectN( 1 ).base_value() );
      }
    }

    void init() override
    {
      warlock_spell_t::init();
      snapshot_flags |= STATE_SP;
    }

    size_t available_targets( std::vector<player_t*>& tl ) const override
    {
      warlock_spell_t::available_targets( tl );

      // Targeting behavior appears to be as follows:
      // 1. If any targets have no current seed (in flight or ticking), they are valid
      // 2. With Sow the Seeds, if at least one target is valid, it will only hit valid targets
      // 3. If no targets are valid according to the above, all targets are instead valid (will refresh DoT on existing target(s) instead)
      bool valid_target = false;
      for ( auto t : tl )
      {
        if ( !( td( t )->dots_seed_of_corruption->is_ticking() || has_travel_events_for( t ) ) )
        {
          valid_target = true;
          break;
        }
      }

      if ( valid_target )
      {
        range::erase_remove( tl, [ this ]( player_t* t ) {
          return ( td( t )->dots_seed_of_corruption->is_ticking() || has_travel_events_for( t ) );
        } );
      }

      return tl.size();
    }

    void execute() override
    {
      warlock_spell_t::execute();

      p()->buffs.cruel_epiphany->decrement();

      p()->buffs.umbrafire_kindling->decrement();
    }

    void impact( action_state_t* s ) override
    {
      if ( result_is_hit( s->result ) )
      {
        td( s->target )->soc_threshold = s->composite_spell_power() * p()->talents.seed_of_corruption->effectN( 1 ).percent();
      }

      warlock_spell_t::impact( s );

      if ( s->chain_target == 0 && p()->buffs.cruel_epiphany->check() )
      {
        td( s->target )->debuffs_cruel_epiphany->trigger();
      }

      if ( p()->sets->has_set_bonus( WARLOCK_AFFLICTION, T31, B4 ) )
      {
        td( s->target )->debuffs_umbrafire_kindling->trigger();
      }
    }

    // If Seed of Corruption is refreshed on a target, it will extend the duration
    // but still explode at the original time, wiping the "DoT". tick() should be used instead
    // of last_tick() to model this appropriately.
    void tick( dot_t* d ) override
    {
      warlock_spell_t::tick( d );

      if ( d->remains() > 0_ms )
        d->cancel();
    }

    void last_tick( dot_t* d ) override
    {
      // Note: We're PROBABLY okay to do this as an if/else if on tier sets because you can't have two separate 4pc bonuses at once
      if ( td( d->target )->debuffs_cruel_epiphany->check() )
      {
        epiphany_explosion->set_target( d->target );
        epiphany_explosion->schedule_execute();
      }
      else if ( td( d->target )->debuffs_umbrafire_kindling->check() )
      {
        make_event( *sim, 0_ms, [this, t = d->target ] { umbrafire_explosion->execute_on_target( t ); } );
      }
      else
      {
        explosion->set_target( d->target );
        explosion->schedule_execute();
      }

      td( d->target )->debuffs_cruel_epiphany->expire();
      td( d->target )->debuffs_umbrafire_kindling->expire();

      warlock_spell_t::last_tick( d );
    }
  };

  struct siphon_life_t : public warlock_spell_t
  {
    siphon_life_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Siphon Life", p, p->talents.siphon_life )
    {
      parse_options( options_str );
    }

    void impact( action_state_t* s ) override
    {
      bool pi_trigger = p()->talents.pandemic_invocation.ok() && td( s->target )->dots_siphon_life->is_ticking()
        && td( s->target )->dots_siphon_life->remains() < p()->talents.pandemic_invocation->effectN( 1 ).time_value();

      warlock_spell_t::impact( s );

      if ( pi_trigger )
        p()->proc_actions.pandemic_invocation_proc->execute_on_target( s->target );
    }
  };

  struct drain_soul_t : public warlock_spell_t
  {
    struct drain_soul_state_t : public action_state_t
    {
      double tick_time_multiplier;

      drain_soul_state_t( action_t* action, player_t* target )
        : action_state_t( action, target ),
        tick_time_multiplier( 1.0 )
      { }

      void initialize() override
      {
        action_state_t::initialize();
        tick_time_multiplier = 1.0;
      }

      std::ostringstream& debug_str( std::ostringstream& s ) override
      {
        action_state_t::debug_str( s ) << " tick_time_multiplier=" << tick_time_multiplier;
        return s;
      }

      void copy_state( const action_state_t* s ) override
      {
        action_state_t::copy_state( s );
        tick_time_multiplier = debug_cast<const drain_soul_state_t*>( s )->tick_time_multiplier;
      }
    };

    drain_soul_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Drain Soul", p, p->talents.drain_soul_dot->ok() ? p->talents.drain_soul_dot : spell_data_t::not_found() )
    {
      parse_options( options_str );
      channeled = true;
    }

    action_state_t* new_state() override
    { return new drain_soul_state_t( this, target ); }

    void snapshot_state( action_state_t* s, result_amount_type rt ) override
    {
      debug_cast<drain_soul_state_t*>( s )->tick_time_multiplier = 1.0 + ( p()->buffs.nightfall->check() ? p()->talents.nightfall_buff->effectN( 3 ).percent() : 0 );
      warlock_spell_t::snapshot_state( s, rt );
    }

    timespan_t tick_time( const action_state_t* s ) const override
    {
      timespan_t t = warlock_spell_t::tick_time( s );

      t *= debug_cast<const drain_soul_state_t*>( s )->tick_time_multiplier;

      return t;
    }

    timespan_t composite_dot_duration( const action_state_t* s ) const override
    {
      double modifier = 1.0;

      if ( p()->buffs.nightfall->check() )
        modifier += p()->talents.nightfall_buff->effectN( 4 ).percent();

      timespan_t dur = dot_duration * ( ( s->haste * modifier * base_tick_time ) / base_tick_time );

      return dur;
    }

    void execute() override
    {
      warlock_spell_t::execute();

      p()->buffs.nightfall->decrement();
    }

    void tick( dot_t* d ) override
    {
      warlock_spell_t::tick( d );

      if ( result_is_hit( d->state->result ) )
      {
        if ( p()->talents.shadow_embrace->ok() )
          td( d->target )->debuffs_shadow_embrace->trigger();

        if ( p()->talents.tormented_crescendo.ok() )
        {
          if ( crescendo_check( p() ) && rng().roll( p()->talents.tormented_crescendo->effectN( 2 ).percent() ) )
          {
            p()->procs.tormented_crescendo->occur();
            p()->buffs.tormented_crescendo->trigger();
          }
        }
      }
    }

    double composite_target_multiplier( player_t* t ) const override
    {
      double m = warlock_spell_t::composite_target_multiplier( t );

      if ( t->health_percentage() < p()->talents.drain_soul_dot->effectN( 3 ).base_value() )
        m *= 1.0 + p()->talents.drain_soul_dot->effectN( 2 ).percent();

      if ( p()->talents.withering_bolt.ok() )
        m *= 1.0 + p()->talents.withering_bolt->effectN( 1 ).percent() * std::min( (int)( p()->talents.withering_bolt->effectN( 2 ).base_value() ), td( t )->count_affliction_dots() );

      return m;
    }
  };

  struct vile_taint_t : public warlock_spell_t
  {
    struct vile_taint_dot_t : public warlock_spell_t
    {
      vile_taint_dot_t( warlock_t* p ) : warlock_spell_t( "Vile Taint (DoT)", p, p->talents.vile_taint_dot )
      {
        tick_zero = background = true;
        execute_action = new agony_t( p, "" );
        execute_action->background = true;
        execute_action->dual = true;
        execute_action->base_costs[ RESOURCE_MANA ] = 0.0;

        if ( p->sets->has_set_bonus( WARLOCK_AFFLICTION, T30, B2 ) )
          base_td_multiplier *= 1.0 + p->sets->set( WARLOCK_AFFLICTION, T30, B2 )->effectN( 4 ).percent();
      }

      void last_tick( dot_t* d ) override
      {
        warlock_spell_t::last_tick( d );

        if ( p()->sets->has_set_bonus( WARLOCK_AFFLICTION, T30, B4 ) )
          td( d->target )->debuffs_infirmity->expire();
      }
    };
    
    vile_taint_t( warlock_t* p, util::string_view options_str ) : warlock_spell_t( "Vile Taint", p, p->talents.vile_taint )
    {
      parse_options( options_str );

      impact_action = new vile_taint_dot_t( p );
      add_child( impact_action );

      if ( p->sets->has_set_bonus( WARLOCK_AFFLICTION, T30, B2 ) )
        cooldown->duration += p->sets->set( WARLOCK_AFFLICTION, T30, B2 )->effectN( 1 ).time_value();
    }

    vile_taint_t( warlock_t* p, util::string_view opt, bool soul_swap ) : vile_taint_t( p, opt )
    {
      if ( soul_swap )
      {
        impact_action->execute_action = nullptr; // Only original Vile Taint triggers secondary effects
        aoe = 1;
      }
    }

    void impact( action_state_t* s ) override
    {
      warlock_spell_t::impact( s );

      if ( p()->sets->has_set_bonus( WARLOCK_AFFLICTION, T30, B4 ) )
        td( s->target )->debuffs_infirmity->trigger();
    }
  };

  struct phantom_singularity_t : public warlock_spell_t
  {
    struct phantom_singularity_tick_t : public warlock_spell_t
    {
      phantom_singularity_tick_t( warlock_t* p )
        : warlock_spell_t( "Phantom Singularity (tick)", p, p->talents.phantom_singularity_tick )
      {
        background = dual = true;
        may_miss = false;
        aoe = -1;

        if ( p->sets->has_set_bonus( WARLOCK_AFFLICTION, T30, B2 ) )
          base_dd_multiplier *= 1.0 + p->sets->set( WARLOCK_AFFLICTION, T30, B2 )->effectN( 3 ).percent();
      }
    };

    phantom_singularity_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Phantom Singularity", p, p->talents.phantom_singularity )
    {
      parse_options( options_str );
      callbacks = false;
      hasted_ticks = true;
      tick_action = new phantom_singularity_tick_t( p );

      spell_power_mod.tick = 0;

      if ( p->sets->has_set_bonus( WARLOCK_AFFLICTION, T30, B2 ) )
        cooldown->duration += p->sets->set( WARLOCK_AFFLICTION, T30, B2 )->effectN( 2 ).time_value();
    }

    void init() override
    {
      warlock_spell_t::init();

      update_flags &= ~STATE_HASTE;
    }

    timespan_t composite_dot_duration( const action_state_t* s ) const override
    { return ( s->action->tick_time( s ) / base_tick_time ) * dot_duration; }

    void impact( action_state_t* s ) override
    {
      warlock_spell_t::impact( s );

      if ( p()->sets->has_set_bonus( WARLOCK_AFFLICTION, T30, B4 ) )
      {
        td( s->target )->debuffs_infirmity->trigger();
      }
    }

    void last_tick( dot_t* d ) override
    {
      warlock_spell_t::last_tick( d );

      if ( p()->sets->has_set_bonus( WARLOCK_AFFLICTION, T30, B4 ) )
      {
        td( d->target )->debuffs_infirmity->expire();
      }
    }
  };

  struct haunt_t : public warlock_spell_t
  {
    haunt_t( warlock_t* p, util::string_view options_str ) : warlock_spell_t( "Haunt", p, p->talents.haunt )
    {
      parse_options( options_str );

      if ( p->talents.seized_vitality->ok() )
        base_dd_multiplier *= 1.0 + p->talents.seized_vitality->effectN( 1 ).percent();
    }

    void impact( action_state_t* s ) override
    {
      warlock_spell_t::impact( s );

      if ( result_is_hit( s->result ) )
      {
        td( s->target )->debuffs_haunt->trigger();
      }
    }
  };

  struct summon_darkglare_t : public warlock_spell_t
  {
    summon_darkglare_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Summon Darkglare", p, p->talents.summon_darkglare )
    {
      parse_options( options_str );

      harmful = callbacks = true; // Set to true because of 10.1 class trinket
      may_crit = may_miss = false;
    }

    void execute() override
    {
      warlock_spell_t::execute();

      timespan_t summon_duration = p()->talents.summon_darkglare->duration();

      if ( p()->talents.malevolent_visionary.ok() )
      {
        summon_duration += p()->talents.malevolent_visionary->effectN( 2 ).time_value();
      }

      p()->warlock_pet_list.darkglares.spawn( summon_duration );

      timespan_t darkglare_extension = timespan_t::from_seconds( p()->talents.summon_darkglare->effectN( 2 ).base_value() );

      darkglare_extension_helper( darkglare_extension );

      p()->buffs.soul_rot->extend_duration( p(), darkglare_extension ); // This dummy buff is active while Soul Rot is ticking
    }

    void darkglare_extension_helper( timespan_t darkglare_extension )
    {
      for ( const auto target : p()->sim->target_non_sleeping_list )
      {
        warlock_td_t* td = p()->get_target_data( target );
        if ( !td )
          continue;

        td->dots_agony->adjust_duration( darkglare_extension );
        td->dots_corruption->adjust_duration( darkglare_extension );
        td->dots_siphon_life->adjust_duration( darkglare_extension );
        td->dots_phantom_singularity->adjust_duration( darkglare_extension );
        td->dots_vile_taint->adjust_duration( darkglare_extension );
        td->dots_unstable_affliction->adjust_duration( darkglare_extension );
        td->dots_soul_rot->adjust_duration( darkglare_extension );
      }
    }
  };

  struct soul_rot_t : public warlock_spell_t
  {
    soul_rot_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Soul Rot", p, p->talents.soul_rot )
    {
      parse_options( options_str );
      aoe = 1 + as<int>( p->talents.soul_rot->effectN( 3 ).base_value() );

      if ( p->sets->has_set_bonus( WARLOCK_AFFLICTION, T31, B2 ) )
        apply_affecting_aura( p->sets->set( WARLOCK_AFFLICTION, T31, B2 ) );

      if ( p->talents.souleaters_gluttony.ok() )
      {
        cooldown->duration += p->talents.souleaters_gluttony->effectN( 1 ).time_value();
      }
    }

    soul_rot_t( warlock_t* p, util::string_view opt, bool soul_swap ) : soul_rot_t( p, opt )
    {
      if ( soul_swap )
      {
        aoe = 1;
      }
    }

    void execute() override
    {
      warlock_spell_t::execute();

      p()->buffs.soul_rot->trigger();

      if ( p()->sets->has_set_bonus( WARLOCK_AFFLICTION, T31, B4 ) )
      {
        p()->buffs.umbrafire_kindling->trigger();
      }
    }

    void impact( action_state_t* s ) override
    {
      warlock_spell_t::impact( s );

      if ( p()->talents.dark_harvest.ok() && aoe > 1 )
      {
        p()->buffs.dark_harvest_haste->trigger();
        p()->buffs.dark_harvest_crit->trigger();
      }
    }

    double composite_ta_multiplier( const action_state_t* s ) const override
    {
      double m = warlock_spell_t::composite_ta_multiplier( s );

      // Note: Soul Swapped Soul Rot technically retains memory of who the primary target was
      // For the moment, we will shortcut this by assuming Soul Swap copy is going on a secondary target
      // TODO: Figure out how to model this appropriately in the case where you copy a secondary and then apply to primary
      if ( s->chain_target == 0 && aoe > 1 )
      {
        m *= 1.0 + p()->talents.soul_rot->effectN( 4 ).base_value() / 10.0; // Primary target takes increased damage
      }

      return m;
    }
  };

  struct soul_flame_t : public warlock_spell_t
  {
    soul_flame_t( warlock_t* p ) : warlock_spell_t( "Soul Flame", p, p->talents.soul_flame_proc )
    {
      background = true;
      aoe = -1;
      reduced_aoe_targets = p->talents.soul_flame->effectN( 4 ).base_value();

      base_dd_multiplier = 1.0 + p->talents.soul_flame->effectN( 2 ).percent();
    }
  };

  struct pandemic_invocation_t : public warlock_spell_t
  {
    pandemic_invocation_t( warlock_t* p ) : warlock_spell_t( "Pandemic Invocation", p, p->talents.pandemic_invocation_proc )
    {
      background = true;

      base_dd_multiplier *= 1.0 + p->talents.pandemic_invocation->effectN( 3 ).percent();
    }

    void execute() override
    {
      warlock_spell_t::execute();

      if ( rng().roll( p()->talents.pandemic_invocation->effectN( 2 ).percent() / 100.0 ) )
      {
        p()->resource_gain( RESOURCE_SOUL_SHARD, 1, p()->gains.pandemic_invocation );
        p()->procs.pandemic_invocation_shard->occur();
      }
    }
  };

  // Affliction Actions End
  // Demonology Actions Begin

  struct hand_of_guldan_t : public warlock_spell_t
  {
    struct umbral_blaze_dot_t : public warlock_spell_t
    {
      umbral_blaze_dot_t( warlock_t* p ) : warlock_spell_t( "Umbral Blaze", p, p->talents.umbral_blaze_dot )
      {
        background = dual = true;
        hasted_ticks = false;
        base_td_multiplier = 1.0 + p->talents.umbral_blaze->effectN( 2 ).percent();
        
        triggers.shadow_invocation_tick = true;
      }
    };

    struct hog_impact_t : public warlock_spell_t
    {
      int shards_used;
      timespan_t meteor_time;
      umbral_blaze_dot_t* blaze;

      hog_impact_t( warlock_t* p )
        : warlock_spell_t( "Hand of Gul'dan (Impact)", p, p->warlock_base.hog_impact ),
        shards_used( 0 ),
        meteor_time( 400_ms )
      {
        aoe = -1;
        dual = true;
        
        triggers.shadow_invocation_direct = true;

        if ( p->talents.umbral_blaze.ok() )
        {
          blaze = new umbral_blaze_dot_t( p );
          add_child( blaze );
        }
      }

      timespan_t travel_time() const override
      { return meteor_time; }

      double action_multiplier() const override
      {
        double m = warlock_spell_t::action_multiplier();

        m *= shards_used;

        if ( p()->buffs.blazing_meteor->check() )
          m *= 1.0 + p()->buffs.blazing_meteor->check_value();

        if ( p()->talents.malefic_impact.ok() )
          m *= 1.0 + p()->talents.malefic_impact->effectN( 1 ).percent();

        return m;
      }

      double composite_crit_chance() const override
      {
        double m = warlock_spell_t::composite_crit_chance();

        if ( p()->talents.malefic_impact.ok() )
          m += p()->talents.malefic_impact->effectN( 2 ).percent();

        return m;
      }

      void execute() override
      {
        warlock_spell_t::execute();

        p()->buffs.blazing_meteor->expire();
      }

      void impact( action_state_t* s ) override
      {
        warlock_spell_t::impact( s );

        // Only trigger Wild Imps once for the original target impact.
        // Still keep it in impact instead of execute because of travel delay.
        if ( result_is_hit( s->result ) && s->target == target )
        {
          // Wild Imp spawns appear to have been sped up in Shadowlands. Last tested 2021-04-16.
          // Current behavior: HoG will spawn a meteor on cast finish. Travel time in spell data is 0.7 seconds.
          // However, damage event occurs before spell effect lands, happening 0.4 seconds after cast.
          // Imps then spawn roughly every 0.18 seconds seconds after the damage event.
          for ( int i = 1; i <= shards_used; i++ )
          {
            auto ev = make_event<imp_delay_event_t>( *sim, p(), rng().gauss( 180.0 * i, 25.0 ), 180.0 * i );
            p()->wild_imp_spawns.push_back( ev );
          }

          if ( p()->talents.umbral_blaze.ok() && rng().roll( p()->talents.umbral_blaze->effectN( 1 ).percent() ) )
          {
            blaze->execute_on_target( s->target );
            p()->procs.umbral_blaze->occur();
          }
        }
      }
    };

    hog_impact_t* impact_spell;

    hand_of_guldan_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Hand of Gul'dan", p, p->warlock_base.hand_of_guldan ),
      impact_spell( new hog_impact_t( p ) )
    {
      parse_options( options_str );

      add_child( impact_spell );
    }

    timespan_t travel_time() const override
    { return 0_ms; }

    timespan_t execute_time() const override
    {
      timespan_t t = warlock_spell_t::execute_time();

      if ( p()->buffs.blazing_meteor->check() )
        t *= 1.0 + p()->tier.blazing_meteor->effectN( 2 ).percent();

      return t;
    }

    bool ready() override
    {
      if ( p()->resources.current[ RESOURCE_SOUL_SHARD ] < 1.0 )
        return false;

      return warlock_spell_t::ready();
    }

    void execute() override
    {
      int shards_used = as<int>( cost() );
      impact_spell->shards_used = shards_used;

      warlock_spell_t::execute();

      if ( p()->talents.demonic_knowledge.ok() && rng().roll( p()->talents.demonic_knowledge->effectN( 1 ).percent() ) )
      {
        p()->buffs.demonic_core->trigger();
        p()->procs.demonic_knowledge->occur();
      }

      if ( p()->talents.dread_calling.ok() )
        p()->buffs.dread_calling->trigger( shards_used );

      if ( p()->sets->has_set_bonus( WARLOCK_DEMONOLOGY, T31, B2 ) )
      {
        for ( const auto target : p()->sim->target_non_sleeping_list )
        {
          warlock_td_t* tdata = td( target );

          if ( !tdata )
            continue;

          if ( tdata->debuffs_doom_brand->check() )
            tdata->debuffs_doom_brand->extend_duration( p(), -p()->sets->set( WARLOCK_DEMONOLOGY, T31, B2 )->effectN( 1 ).time_value() * shards_used );
        }
      }
    }

    void consume_resource() override
    {
      warlock_spell_t::consume_resource();

      if ( last_resource_cost == 1.0 )
        p()->procs.one_shard_hog->occur();
      if ( last_resource_cost == 2.0 )
        p()->procs.two_shard_hog->occur();
      if ( last_resource_cost == 3.0 )
        p()->procs.three_shard_hog->occur();
    }

    void impact( action_state_t* s ) override
    {
      warlock_spell_t::impact( s );

      impact_spell->execute_on_target( s->target );

      if ( p()->talents.pact_of_the_imp_mother.ok() && rng().roll( p()->talents.pact_of_the_imp_mother->effectN( 1 ).percent() ) )
      {
        make_event( *sim, 0_ms, [this, t = target ]{ impact_spell->execute_on_target( t ); } );
        p()->procs.pact_of_the_imp_mother->occur();
      }
    }
  };

  struct demonbolt_t : public warlock_spell_t
  {
    demonbolt_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Demonbolt", p, p->talents.demoniac.ok() ? p->talents.demonbolt_spell : spell_data_t::not_found() )
    {
      parse_options( options_str );

      energize_type = action_energize::ON_CAST;
      energize_resource = RESOURCE_SOUL_SHARD;
      energize_amount = 2.0;

      triggers.shadow_invocation_direct = true;
    }

    timespan_t execute_time() const override
    {
      timespan_t t = warlock_spell_t::execute_time();

      if ( p()->buffs.demonic_core->check() )
        t *= 1.0 + p()->talents.demonic_core_buff->effectN( 1 ).percent();

      return t;
    }

    void execute() override
    {
      warlock_spell_t::execute();

      p()->buffs.demonic_core->up(); // For benefit tracking

      if ( p()->buffs.demonic_core->check() )
      {
        if ( p()->talents.spiteful_reconstitution.ok() && rng().roll( 0.3 ) )
        {
          p()->warlock_pet_list.wild_imps.spawn( 1u );
          p()->procs.spiteful_reconstitution->occur();
        }

        if ( p()->talents.immutable_hatred.ok() )
        {
          auto active_pet = p()->warlock_pet_list.active;

          if ( active_pet->pet_type == PET_FELGUARD )
            debug_cast<pets::demonology::felguard_pet_t*>( active_pet )->hatred_proc->execute_on_target( execute_state->target );
        }

        if ( p()->sets->has_set_bonus( WARLOCK_DEMONOLOGY, T31, B2 ) )
          td( target )->debuffs_doom_brand->trigger();
      }
      else
      {
        if ( p()->talents.immutable_hatred.ok() && p()->bugs )
        {
          auto active_pet = p()->warlock_pet_list.active;

          if ( active_pet->pet_type == PET_FELGUARD )
            debug_cast<pets::demonology::felguard_pet_t*>( active_pet )->hatred_proc->execute_on_target( execute_state->target );
        }
      }

      p()->buffs.demonic_core->decrement();

      if ( p()->sets->has_set_bonus( WARLOCK_DEMONOLOGY, T30, B2 ) )
      {
        timespan_t reduction = -p()->sets->set( WARLOCK_DEMONOLOGY, T30, B2 )->effectN( 2 ).time_value();

        p()->cooldowns.grimoire_felguard->adjust( reduction );
      }

      if ( p()->talents.power_siphon.ok() )
        p()->buffs.power_siphon->decrement();

      if ( p()->talents.demonic_calling.ok() )
        p()->buffs.demonic_calling->trigger();

      p()->buffs.stolen_power_final->expire();

      if ( p()->sets->has_set_bonus( WARLOCK_DEMONOLOGY, T29, B4 ) && rng().roll( 0.3 ) )
      {
        p()->buffs.blazing_meteor->trigger();
        p()->procs.blazing_meteor->occur();
      }
    }

    double action_multiplier() const override
    {
      double m = warlock_spell_t::action_multiplier();

      if ( p()->talents.sacrificed_souls.ok() )
        m *= 1.0 + p()->talents.sacrificed_souls->effectN( 1 ).percent() * p()->active_demon_count();
      
      if ( p()->talents.power_siphon.ok() )
        m *= 1.0 + p()->buffs.power_siphon->check_value();

      if ( p()->talents.shadows_bite.ok() )
        m *= 1.0 + p()->buffs.shadows_bite->check_value();

      if ( p()->talents.stolen_power.ok() && p()->buffs.stolen_power_final->check() )
        m *= 1.0 + p()->talents.stolen_power_final_buff->effectN( 2 ).percent();

      if ( p()->sets->has_set_bonus( WARLOCK_DEMONOLOGY, T29, B2 ) )
        m *= 1.0 + p()->sets->set( WARLOCK_DEMONOLOGY, T29, B2 )->effectN( 1 ).percent();

      if ( p()->sets->has_set_bonus( WARLOCK_DEMONOLOGY, T30, B2 ) )
        m *= 1.0 + p()->sets->set( WARLOCK_DEMONOLOGY, T30, B2 )->effectN( 1 ).percent();

      return m;
    }
  };

  struct implosion_t : public warlock_spell_t
  {
    struct implosion_aoe_t : public warlock_spell_t
    {
      double energy_remaining = 0.0;
      warlock_pet_t* next_imp;

      implosion_aoe_t( warlock_t* p )
        : warlock_spell_t( "Implosion (AoE)", p, p->talents.implosion_aoe )
      {
        aoe = -1;
        background = dual = true;
        callbacks = false;
      }

      double action_multiplier() const override
      {
        double m = warlock_spell_t::action_multiplier();

        if ( debug_cast<pets::demonology::wild_imp_pet_t*>( next_imp )->buffs.imp_gang_boss->check() )
          m *= 1.0 + p()->talents.imp_gang_boss->effectN( 2 ).percent();

        if ( p()->talents.spiteful_reconstitution.ok() )
          m *= 1.0 + p()->talents.spiteful_reconstitution->effectN( 1 ).percent();

        return m;
      }

      double composite_target_multiplier( player_t* t ) const override
      {
        double m = warlock_spell_t::composite_target_multiplier( t );

        if ( t == target )
          m *= ( energy_remaining / 100.0 );

        return m;
      }

      void execute() override
      {
        warlock_spell_t::execute();
        next_imp->dismiss();
      }
    };

    implosion_aoe_t* explosion;

    implosion_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Implosion", p, p->talents.implosion ),
      explosion( new implosion_aoe_t( p ) )
    {
      parse_options( options_str );
      add_child( explosion );
    }

    bool ready() override
    { return warlock_spell_t::ready() && p()->warlock_pet_list.wild_imps.n_active_pets() > 0; }

    void execute() override
    {
      // Travel speed is not in spell data, in game test appears to be 65 yds/sec as of 2020-12-04
      timespan_t imp_travel_time = calc_imp_travel_time( 65 );

      int launch_counter = 0;
      for ( auto imp : p()->warlock_pet_list.wild_imps )
      {
        if ( !imp->is_sleeping() )
        {
          implosion_aoe_t* ex = explosion;
          player_t* tar = target;
          double dist = p()->get_player_distance( *tar );

          imp->trigger_movement( dist, movement_direction_type::TOWARDS );
          imp->interrupt();
          imp->imploded = true;

          // Imps launched with Implosion appear to be staggered and snapshot when they impact
          // 2020-12-04: Implosion may have been made quicker in Shadowlands, too fast to easily discern with combat log
          // Going to set the interval to 10 ms, which should keep all but the most extreme imp counts from bleeding into the next GCD
          // TODO: There's an awkward possibility of Implosion seeming "ready" after casting it if all the imps have not imploded yet. Find a workaround
          make_event( sim, 50_ms * launch_counter + imp_travel_time, [ ex, tar, imp ] {
            if ( imp && !imp->is_sleeping() )
            {
              ex->energy_remaining = ( imp->resources.current[ RESOURCE_ENERGY ] );
              ex->set_target( tar );
              ex->next_imp = imp;
              ex->execute();
            }
          } );

          launch_counter++;
        }
      }

      warlock_spell_t::execute();
    }

    timespan_t calc_imp_travel_time( double speed )
    {
      double t = 0;

      if ( speed > 0 )
      {
        double distance = player->get_player_distance( *target );

        if ( distance > 0 )
          t += distance / speed;
      }

      double v = sim->travel_variance;

      if ( v )
        t = rng().gauss( t, v );

      t = std::max( t, min_travel_time );

      return timespan_t::from_seconds( t );
    }
  };

  struct call_dreadstalkers_t : public warlock_spell_t
  {
    call_dreadstalkers_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Call Dreadstalkers", p, p->talents.call_dreadstalkers )
    {
      parse_options( options_str );
      may_crit = false;
      affected_by.soul_conduit_base_cost = true;
    }

    double cost_pct_multiplier() const override
    {
      double m = warlock_spell_t::cost_pct_multiplier();

      if ( p()->buffs.demonic_calling->check() )
        m *= 1.0 + p()->talents.demonic_calling_buff->effectN( 1 ).percent();

      return m;
    }

    timespan_t execute_time() const override
    {
      timespan_t t = warlock_spell_t::execute_time();

      if ( p()->buffs.demonic_calling->check() )
        t *= 1.0 + p()->talents.demonic_calling_buff->effectN( 2 ).percent();

      return t;
    }

    void execute() override
    {
      warlock_spell_t::execute();

      unsigned count = as<unsigned>( p()->talents.call_dreadstalkers->effectN( 1 ).base_value() );

      // Set a randomized offset on first melee attacks after travel time. Make sure it's the same value for each dog so they're synced
      timespan_t delay = rng().range( 0_s, 1_s );

      timespan_t dur_adjust = duration_adjustment( delay );

      auto dogs = p()->warlock_pet_list.dreadstalkers.spawn( p()->talents.call_dreadstalkers_2->duration() + dur_adjust, count );

      for ( auto d : dogs )
      {
        if ( d->is_active() )
        {
          d->server_action_delay = delay;

          if ( p()->talents.dread_calling.ok() && !d->buffs.dread_calling->check() )
            d->buffs.dread_calling->trigger( 1, p()->buffs.dread_calling->check_stack_value() );
        }
      }

      if ( p()->buffs.demonic_calling->up() )
        p()->buffs.demonic_calling->decrement();

      p()->buffs.dread_calling->expire();
    }

    timespan_t duration_adjustment( timespan_t delay )
    {
      // Despawn events appear to be offset from the melee attack check in a correlated manner
      // Starting with this function which mimics despawns on the "off-beats" compared to the 1s heartbeat for the melee attack
      // This may require updating if better understanding is found for the behavior, such as a fudge from Blizzard related to player distance
      return ( delay + 500_ms ) % 1_s;
    }
  };

  struct bilescourge_bombers_t : public warlock_spell_t
  {
    struct bilescourge_bombers_tick_t : public warlock_spell_t
    {
      bilescourge_bombers_tick_t( warlock_t* p )
        : warlock_spell_t( "Bilescourge Bombers (tick)", p, p->talents.bilescourge_bombers_aoe )
      {
        aoe = -1;
        background = dual = direct_tick = true;
        callbacks = false;
        radius = p->talents.bilescourge_bombers->effectN( 1 ).radius();

        base_dd_multiplier *= 1.0 + p->talents.shadow_invocation->effectN( 1 ).percent();
      }
    };

    bilescourge_bombers_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Bilescourge Bombers", p, p->talents.bilescourge_bombers )
    {
      parse_options( options_str );

      dot_duration = 0_ms;
      may_miss = may_crit = false;
      base_tick_time = 500_ms;

      if ( !p->proc_actions.bilescourge_bombers_aoe_tick )
      {
        p->proc_actions.bilescourge_bombers_aoe_tick = new bilescourge_bombers_tick_t( p );
        p->proc_actions.bilescourge_bombers_aoe_tick->stats = stats;
      }
    }

    void execute() override
    {
      warlock_spell_t::execute();

      make_event<ground_aoe_event_t>( *sim, p(),
                                      ground_aoe_params_t()
                                        .target( execute_state->target )
                                        .x( execute_state->target->x_position )
                                        .y( execute_state->target->y_position )
                                        .pulse_time( base_tick_time )
                                        .duration( p()->talents.bilescourge_bombers->duration() )
                                        .start_time( sim->current_time() )
                                        .action( p()->proc_actions.bilescourge_bombers_aoe_tick ) );
    }
  };

  struct bilescourge_bombers_proc_t : public warlock_spell_t
  {
    bilescourge_bombers_proc_t( warlock_t* p )
      : warlock_spell_t( "Bilescourge Bombers (proc)", p, p->find_spell( 267213 ) )
    {
      aoe = -1;
      background = dual = direct_tick = true;
      callbacks = false;
      radius = p->find_spell( 267211 )->effectN( 1 ).radius();

      base_dd_multiplier *= 1.0 + p->talents.shadow_invocation->effectN( 1 ).percent();
    }
  };

  struct demonic_strength_t : public warlock_spell_t
  {
    demonic_strength_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Demonic Strength", p, p->talents.demonic_strength )
    {
      parse_options( options_str );

      internal_cooldown = p->get_cooldown( "felstorm_icd" );
    }

    bool ready() override
    {
      auto active_pet = p()->warlock_pet_list.active;

      if ( !active_pet )
        return false;

      if ( active_pet->pet_type != PET_FELGUARD )
        return false;

      return warlock_spell_t::ready();
    }

    void execute() override
    {
      auto active_pet = p()->warlock_pet_list.active;
      
      warlock_spell_t::execute();

      if ( active_pet->pet_type == PET_FELGUARD )
      {
        active_pet->buffs.demonic_strength->trigger();

        debug_cast<pets::demonology::felguard_pet_t*>( active_pet )->queue_ds_felstorm();

        internal_cooldown->start( 5_s * p()->composite_spell_haste() );
      }
    }
  };

  struct power_siphon_t : public warlock_spell_t
  {
    power_siphon_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Power Siphon", p, p->talents.power_siphon )
    {
      parse_options( options_str );

      harmful = false;
      ignore_false_positive = true;

      target = player;
    }

    bool ready() override
    {
      if ( is_precombat && p()->talents.inner_demons.ok() )
        return warlock_spell_t::ready();

      if ( p()->warlock_pet_list.wild_imps.n_active_pets() < 1 )
        return false;

      return warlock_spell_t::ready();
    }

    void execute() override
    {
      warlock_spell_t::execute();

      if ( is_precombat )
      {
        p()->buffs.power_siphon->trigger( 2, p()->talents.power_siphon_buff->duration() );
        p()->buffs.demonic_core->trigger( 2, p()->talents.demonic_core_buff->duration() );
        
        return;
      }

      auto imps = p()->warlock_pet_list.wild_imps.active_pets();

      range::sort(
        imps, []( const pets::demonology::wild_imp_pet_t* imp1, const pets::demonology::wild_imp_pet_t* imp2 ) {
          double lv = imp1->resources.current[ RESOURCE_ENERGY ];
          double rv = imp2->resources.current[ RESOURCE_ENERGY ];

          // Power Siphon deprioritizes Wild Imps that are Gang Bosses or empowered by Summon Demonic Tyrant
          // Padding ensures they still sort in order at the back of the list
          lv += ( imp1->buffs.imp_gang_boss->check() || imp1->buffs.demonic_power->check() ) ? 200.0 : 0.0;
          rv += ( imp2->buffs.imp_gang_boss->check() || imp2->buffs.demonic_power->check() ) ? 200.0 : 0.0;

          if ( lv == rv )
          {
            timespan_t lr = imp1->expiration->remains();
            timespan_t rr = imp2->expiration->remains();
            if ( lr == rr )
            {
              return imp1->actor_spawn_index < imp2->actor_spawn_index;
            }

            return lr < rr;
          }

          return lv < rv;
        } );

      unsigned max_imps = as<int>( p()->talents.power_siphon->effectN( 1 ).base_value() );

      if ( imps.size() > max_imps )
        imps.resize( max_imps );

      while ( !imps.empty() )
      {
        p()->buffs.power_siphon->trigger();
        p()->buffs.demonic_core->trigger();
        pets::demonology::wild_imp_pet_t* imp = imps.front();
        imps.erase( imps.begin() );
        imp->power_siphon = true;
        imp->dismiss();
      }
    }
  };

  struct summon_demonic_tyrant_t : public warlock_spell_t
  {
    summon_demonic_tyrant_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Summon Demonic Tyrant", p, p->talents.summon_demonic_tyrant )
    {
      parse_options( options_str );

      harmful = true; // Set to true because of 10.1 class trinket
      may_crit = false;
    }

    void execute() override
    {
      warlock_spell_t::execute();
      
      // Last tested 2021-07-13
      // There is a chance for tyrant to get an extra cast off before reaching the required haste breakpoint.
      // In-game testing found this can be modelled fairly closely using a normal distribution.
      timespan_t extraTyrantTime = rng().gauss<380,220>();
      auto tyrants = p()->warlock_pet_list.demonic_tyrants.spawn( data().duration() + extraTyrantTime );

      if ( p()->talents.soulbound_tyrant.ok() )
        p()->resource_gain( RESOURCE_SOUL_SHARD, p()-> talents.soulbound_tyrant->effectN( 1 ).base_value() / 10.0, p()->gains.soulbound_tyrant );

      timespan_t extension_time = p()->talents.demonic_power_buff->effectN( 3 ).time_value();

      int wild_imp_counter = 0;
      int demon_counter = 0;
      int imp_cap = as<int>( p()->talents.summon_demonic_tyrant->effectN( 3 ).base_value() + p()->talents.reign_of_tyranny->effectN( 1 ).base_value() );

      for ( auto& pet : p()->pet_list )
      {
        auto lock_pet = dynamic_cast<warlock_pet_t*>( pet );
        pet_e pet_type = lock_pet->pet_type;

        if ( lock_pet == nullptr )
          continue;

        if ( lock_pet->is_sleeping() )
          continue;

        if ( pet_type == PET_DEMONIC_TYRANT )
          continue;

        if ( pet_type == PET_WILD_IMP && wild_imp_counter < imp_cap )
        {
          if ( lock_pet->expiration )
            lock_pet->expiration->reschedule_time = lock_pet->expiration->time + extension_time;

          lock_pet->buffs.demonic_power->trigger();
          wild_imp_counter++;
          demon_counter++;
        }
        else if ( pet_type == PET_DREADSTALKER || pet_type == PET_VILEFIEND || pet_type == PET_SERVICE_FELGUARD || lock_pet->is_main_pet )
        {
          if ( lock_pet->expiration )
            lock_pet->expiration->reschedule_time = lock_pet->expiration->time + extension_time;

          lock_pet->buffs.demonic_power->trigger();
          demon_counter++;
        }
      }

      p()->buffs.tyrant->trigger();

      if ( p()->buffs.dreadstalkers->check() )
        p()->buffs.dreadstalkers->extend_duration( p(), extension_time );

      if ( p()->buffs.grimoire_felguard->check() )
      {
        p()->buffs.grimoire_felguard->extend_duration( p(), extension_time );

        if ( p()->sets->has_set_bonus( WARLOCK_DEMONOLOGY, T30, B4 ) )
          p()->buffs.rite_of_ruvaraad->extend_duration( p(), extension_time );
      }

      if ( p()->buffs.vilefiend->check() )
        p()->buffs.vilefiend->extend_duration( p(), extension_time );

      if ( p()->talents.reign_of_tyranny.ok() )
      {
        for ( auto t : tyrants )
        {
          if ( t->is_active() )
            t->buffs.reign_of_tyranny->trigger( demon_counter );
        }
      }
    }
  };

  struct grimoire_felguard_t : public warlock_spell_t
  {
    grimoire_felguard_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Grimoire: Felguard", p, p->talents.grimoire_felguard )
    {
      parse_options( options_str );

      harmful = may_crit = false;
    }

    void execute() override
    {
      warlock_spell_t::execute();

      p()->warlock_pet_list.grimoire_felguards.spawn( p()->talents.grimoire_felguard->duration() );
      p()->buffs.grimoire_felguard->trigger();
    }
  };

  struct doom_t : public warlock_spell_t
  {
    doom_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Doom", p, p->talents.doom )
    {
      parse_options( options_str );

      energize_type = action_energize::PER_TICK;
      energize_resource = RESOURCE_SOUL_SHARD;
      energize_amount = 1.0;

      hasted_ticks = true;

      triggers.shadow_invocation_tick = true;
    }

    timespan_t composite_dot_duration( const action_state_t* s ) const override
    { return s->action->tick_time( s ); }

    double composite_ta_multiplier( const action_state_t* s ) const override
    {
      double m = warlock_spell_t::composite_ta_multiplier( s );

      if ( p()->talents.kazaaks_final_curse.ok() )
        m *= 1.0 + td( s->target )->debuffs_kazaaks_final_curse->check_value();

      return m;
    }

    void impact( action_state_t* s ) override
    {
      warlock_spell_t::impact( s );

      if ( p()->talents.kazaaks_final_curse.ok() )
        td( s->target )->debuffs_kazaaks_final_curse->trigger( 1, pet_counter() * p()->talents.kazaaks_final_curse->effectN( 1 ).percent() );
    }

    void last_tick( dot_t* d ) override
    {
      if ( d->time_to_next_full_tick() > 0_ms )
        gain_energize_resource( RESOURCE_SOUL_SHARD, energize_amount, p()->gains.doom );

      warlock_spell_t::last_tick( d );

      td( d->target )->debuffs_kazaaks_final_curse->expire();
    }

  private:
    int pet_counter()
    {
      int count = 0;

      for ( auto& pet : p()->pet_list )
      {
        auto lock_pet = debug_cast<warlock_pet_t*>( pet );
        pet_e pet_type = lock_pet->pet_type;

        if ( lock_pet == nullptr )
          continue;

        if ( lock_pet->is_sleeping() )
          continue;

        if ( pet_type == PET_DEMONIC_TYRANT || pet_type == PET_PIT_LORD || pet_type == PET_WARLOCK_RANDOM )
          continue;

        if ( pet_type == PET_VILEFIEND )
          continue;

        count++;
      }

      return count;
    }
  };

  struct summon_vilefiend_t : public warlock_spell_t
  {
    summon_vilefiend_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Summon Vilefiend", p, p->talents.summon_vilefiend )
    {
      parse_options( options_str );

      harmful = may_crit = false;

      if ( p->talents.fel_invocation.ok() )
        base_execute_time += p->talents.fel_invocation->effectN( 2 ).time_value();
    }

    void execute() override
    {
      warlock_spell_t::execute();
      
      p()->buffs.vilefiend->trigger();
      p()->warlock_pet_list.vilefiends.spawn( p()->talents.summon_vilefiend->duration() );
    }
  };

  struct guillotine_t : public warlock_spell_t
  {
    guillotine_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Guillotine", p, p->talents.guillotine )
    {
      parse_options( options_str );
      
      may_crit = false;
      internal_cooldown = p->get_cooldown( "felstorm_icd" );
    }

    bool ready() override
    {
      auto active_pet = p()->warlock_pet_list.active;

      if ( !active_pet )
        return false;

      if ( active_pet->pet_type != PET_FELGUARD )
        return false;

      return warlock_spell_t::ready();
    }

    void execute() override
    {
      auto active_pet = p()->warlock_pet_list.active;

      warlock_spell_t::execute();

      if ( active_pet->pet_type == PET_FELGUARD )
      {
        active_pet->buffs.fiendish_wrath->trigger();

        debug_cast<pets::demonology::felguard_pet_t*>( active_pet )->felguard_guillotine->execute_on_target( execute_state->target );

        internal_cooldown->start( 6_s );
      }
    }
  };

  struct doom_brand_t : public warlock_spell_t
  {
    doom_brand_t( warlock_t* p ) : warlock_spell_t( "Doom Brand", p, p->tier.doom_brand_aoe )
    {
      aoe = -1;
      reduced_aoe_targets = 8.0;
      background = dual = true;
      callbacks = false;
    }

    void execute() override
    {
      warlock_spell_t::execute();

      if ( p()->sets->has_set_bonus( WARLOCK_DEMONOLOGY, T31, B4 ) )
      {
        double increment_max = 0.5;

        int debuff_count = 1;
        for ( auto t : target_list() )
        {
          if ( td( t )->debuffs_doom_brand->check() )
            debuff_count++;
        }

        increment_max *= std::pow( debuff_count, -1.0 / 2.0 );

        p()->doom_brand_accumulator += rng().range( 0.0, increment_max );

        if ( p()->doom_brand_accumulator >= 1.0 )
        {
          p()->warlock_pet_list.doomfiends.spawn();
          p()->procs.doomfiend->occur();
          p()->doom_brand_accumulator -= 1.0;
        }
      }
    }

    double composite_da_multiplier( const action_state_t* s ) const override
    {
      double m = warlock_spell_t::composite_da_multiplier( s );

      if ( s->n_targets == 1 )
        m *= 1.0 + p()->sets->set( WARLOCK_DEMONOLOGY, T31, B2 )->effectN( 2 ).percent();

      return m;
    }
  };

  // Demonology Actions End
  // Destruction Actions Begin

  struct incinerate_t : public warlock_spell_t
  {
    struct incinerate_state_t final : public action_state_t
    {
      int total_hit;

      incinerate_state_t( action_t* action, player_t* target )
        : action_state_t( action, target ),
        total_hit( 0 )
      { }

      void initialize() override
      {
        action_state_t::initialize();
        total_hit = 0;
      }

      std::ostringstream& debug_str( std::ostringstream& s ) override
      {
        action_state_t::debug_str( s ) << " total_hit=" << total_hit;
        return s;
      }

      void copy_state( const action_state_t* s ) override
      {
        action_state_t::copy_state( s );
        total_hit = debug_cast<const incinerate_state_t*>( s )->total_hit;
      }
    };

    struct incinerate_fnb_t : public warlock_spell_t
    {
      incinerate_fnb_t( warlock_t* p )
        : warlock_spell_t( "Incinerate (Fire and Brimstone)", p, p->warlock_base.incinerate )
      {
        aoe = -1;
        background = dual = true;

        affected_by.chaotic_energies = true;

        base_multiplier *= p->talents.fire_and_brimstone->effectN( 1 ).percent();
      }

      void init() override
      {
        warlock_spell_t::init();

        p()->havoc_spells.push_back( this ); // Needed for proper target list invalidation
      }

      action_state_t* new_state() override
      { return new incinerate_state_t( this, target ); }

      void snapshot_state( action_state_t* s, result_amount_type rt ) override
      {
        debug_cast<incinerate_state_t*>( s )->total_hit = p()->incinerate_last_target_count;
        
        warlock_spell_t::snapshot_state( s, rt );
      }

      double cost() const override
      { return 0.0; }

      size_t available_targets( std::vector<player_t*>& tl ) const override
      {
        warlock_spell_t::available_targets( tl );

        auto it = range::find( tl, target );
        if ( it != tl.end() )
          tl.erase( it );

        it = range::find( tl, p()->havoc_target );
        if ( it != tl.end() )
          tl.erase( it );

        return tl.size();
      }

      double action_multiplier() const override
      {
        double m = warlock_spell_t::action_multiplier();

        if ( p()->talents.burn_to_ashes.ok() )
          m *= 1.0 + p()->buffs.burn_to_ashes->check_value();

        return m;
      }

      double composite_target_multiplier( player_t* t ) const override
      {
        double m = warlock_spell_t::composite_target_multiplier( t );

        if ( p()->talents.ashen_remains.ok() && td( t )->dots_immolate->is_ticking() )
          m *= 1.0 + p()->talents.ashen_remains->effectN( 1 ).percent();

        return m;
      }

      void impact( action_state_t* s ) override
      {
        warlock_spell_t::impact( s );

        if ( s->result == RESULT_CRIT )
          p()->resource_gain( RESOURCE_SOUL_SHARD, 0.1, p()->gains.incinerate_fnb_crits );

        if ( p()->sets->has_set_bonus( WARLOCK_DESTRUCTION, T30, B2 ) )
        {
          auto inc_state = debug_cast< incinerate_state_t*>( s );
          double increment_max = 0.4;

          increment_max *= std::pow( inc_state->total_hit, -2.0 / 3.0 );

          p()->cdf_accumulator += rng().range( 0.0, increment_max );

          if ( p()->cdf_accumulator >= 1.0 )
          {
            p()->proc_actions.channel_demonfire->execute_on_target( inc_state->target );
            p()->procs.channel_demonfire->occur();
            p()->cdf_accumulator -= 1.0;
          }
        }
      }
    };

    double energize_mult;
    incinerate_fnb_t* fnb_action;

    incinerate_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Incinerate", p, p->warlock_base.incinerate ),
      fnb_action( new incinerate_fnb_t( p ) )
    {
      parse_options( options_str );

      energize_type = action_energize::PER_HIT;
      energize_resource = RESOURCE_SOUL_SHARD;
      energize_amount = ( p->warlock_base.incinerate_energize->effectN( 1 ).base_value() ) / 10.0;
      
      energize_mult = 1.0 + p->talents.diabolic_embers->effectN( 1 ).percent();
      energize_amount *= energize_mult;

      affected_by.chaotic_energies = true;
      affected_by.havoc = true;

      add_child( fnb_action );
    }

    action_state_t* new_state() override
    { return new incinerate_state_t( this, target ); }

    void snapshot_state( action_state_t* s, result_amount_type rt ) override
    {
      debug_cast<incinerate_state_t*>( s )->total_hit = p()->incinerate_last_target_count;
      warlock_spell_t::snapshot_state( s, rt );
    }

    timespan_t execute_time() const override
    {
      timespan_t t = warlock_spell_t::execute_time();

      if ( p()->buffs.backdraft->check() )
        t *= 1.0 + p()->talents.backdraft_buff->effectN( 1 ).percent();

      return t;
    }

    timespan_t gcd() const override
    {
      timespan_t t = warlock_spell_t::gcd();

      if ( t == 0_ms )
        return t;

      if ( p()->buffs.backdraft->check() )
        t *= 1.0 + p()->talents.backdraft_buff->effectN( 2 ).percent();

      if ( t < min_gcd )
        t = min_gcd;

      return t;
    }

    void execute() override
    {
      // Update and store target count for RNG effects that trigger off Incinerate hits
      int n = std::max( n_targets(), 1 );
      if ( p()->talents.fire_and_brimstone.ok() )
        n += as<int>( fnb_action->target_list().size() );
      p()->incinerate_last_target_count = n;

      warlock_spell_t::execute();
      
      p()->buffs.backdraft->decrement();

      if ( p()->talents.fire_and_brimstone.ok() )
        fnb_action->execute_on_target( target );

      if ( p()->talents.decimation.ok() && target->health_percentage() <= p()->talents.decimation->effectN( 2 ).base_value() )
        p()->cooldowns.soul_fire->adjust( p()->talents.decimation->effectN( 1 ).time_value() );

      p()->buffs.burn_to_ashes->decrement(); // Must do after Fire and Brimstone execute so that child picks up buff
    }

    void impact( action_state_t* s ) override
    {
      warlock_spell_t::impact( s );

      if ( s->result == RESULT_CRIT )
        p()->resource_gain( RESOURCE_SOUL_SHARD, 0.1 * energize_mult, p()->gains.incinerate_crits );

      if ( p()->sets->has_set_bonus( WARLOCK_DESTRUCTION, T30, B2 ) )
      {
        auto inc_state = debug_cast<incinerate_state_t*>( s );
        double increment_max = 0.4;

        increment_max *= std::pow( inc_state->total_hit, -2.0 / 3.0 );

        p()->cdf_accumulator += rng().range( 0.0, increment_max );

        if ( p()->cdf_accumulator >= 1.0 )
        {
          p()->proc_actions.channel_demonfire->execute_on_target( inc_state->target );
          p()->procs.channel_demonfire->occur();
          p()->cdf_accumulator -= 1.0;
        }
      }
    }

    double action_multiplier() const override
    {
      double m = warlock_spell_t::action_multiplier();

      if ( p()->talents.burn_to_ashes.ok() )
        m *= 1.0 + p()->buffs.burn_to_ashes->check_value();

      return m;
    }

    double composite_target_multiplier( player_t* t ) const override
    {
      double m = warlock_spell_t::composite_target_multiplier( t );

      if ( p()->talents.ashen_remains.ok() && td( t )->dots_immolate->is_ticking() )
        m *= 1.0 + p()->talents.ashen_remains->effectN( 1 ).percent();

      return m;
    }
  };

  struct immolate_t : public warlock_spell_t
  {
    struct immolate_dot_t : public warlock_spell_t
    {
      immolate_dot_t( warlock_t* p )
        : warlock_spell_t( "Immolate", p, p->warlock_base.immolate_dot )
      {
        background = dual = true;

        affected_by.chaotic_energies = true;

        // TOCHECK: Is this necessary?
        spell_power_mod.tick = p->warlock_base.immolate_dot->effectN( 1 ).sp_coeff();
        dot_duration += p->talents.improved_immolate->effectN( 1 ).time_value();
        base_multiplier *= 1.0 + p->talents.scalding_flames->effectN( 2 ).percent();
      }

      void tick( dot_t* d ) override
      {
        warlock_spell_t::tick( d );

        if ( d->state->result == RESULT_CRIT && rng().roll( p()->warlock_base.immolate->effectN( 2 ).percent() ) )
          p()->resource_gain( RESOURCE_SOUL_SHARD, 0.1, p()->gains.immolate_crits );

        p()->resource_gain( RESOURCE_SOUL_SHARD, 0.1, p()->gains.immolate );

        if ( p()->talents.flashpoint.ok() && d->state->target->health_percentage() >= p()->talents.flashpoint->effectN( 2 ).base_value() )
          p()->buffs.flashpoint->trigger();

        if ( p()->sets->has_set_bonus( WARLOCK_DESTRUCTION, T30, B2 ) )
        {
          double increment_max = 0.07;

          increment_max *= std::pow( p()->get_active_dots( d ), -2.0 / 3.0 );

          p()->cdf_accumulator += rng().range( 0.0, increment_max );

          if ( p()->cdf_accumulator >= 1.0 )
          {
            p()->proc_actions.channel_demonfire->execute_on_target( d->target );
            p()->procs.channel_demonfire->occur();
            p()->cdf_accumulator -= 1.0;
          }
        }

        if ( p()->sets->has_set_bonus( WARLOCK_DESTRUCTION, T31, B2 ) )
        {  
          double increment_max = 0.16;

          increment_max *= std::pow( p()->get_active_dots( d ), -4.0 / 9.0 );

          p()->dimensional_accumulator += rng().range( 0.0, increment_max );

          if ( p()->dimensional_accumulator >= 1.0 )
          {
            p()->cooldowns.dimensional_rift->reset( true, 1 );
            p()->procs.dimensional_refund->occur();
            p()->dimensional_accumulator -= 1.0;
          }
        }
      }

      void last_tick( dot_t* d ) override
      {
        if ( p()->get_active_dots( d ) == 1 )
          p()->dimensional_accumulator = rng().range( 0.0, 0.99 );
        
        warlock_spell_t::last_tick( d );
      }
    };

    immolate_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Immolate (direct)", p, p->warlock_base.immolate )
    {
      parse_options( options_str );

      affected_by.chaotic_energies = true;
      affected_by.havoc = true;

      impact_action = new immolate_dot_t( p );
      add_child( impact_action );

      base_multiplier *= 1.0 + p->talents.scalding_flames->effectN( 1 ).percent();
    }

    dot_t* get_dot( player_t* t ) override
    { return impact_action->get_dot( t ); }
  };

  struct internal_combustion_t : public warlock_spell_t
  {
    internal_combustion_t( warlock_t* p )
      : warlock_spell_t( "Internal Combustion", p, p->talents.internal_combustion )
    {
      background = dual = true;
    }

    void init() override
    {
      warlock_spell_t::init();

      snapshot_flags &= STATE_NO_MULTIPLIER;
    }

    void execute() override
    {
      dot_t* dot = td( target )->dots_immolate;

      assert( dot->current_action );
      action_state_t* state = dot->current_action->get_state( dot->state );
      dot->current_action->calculate_tick_amount( state, 1.0 );

      double tick_base_damage = state->result_raw;

      if ( td( target )->debuffs_conflagrate->up() )
        tick_base_damage /= 1.0 + td( target )->debuffs_conflagrate->check_value();

      timespan_t remaining = std::min( dot->remains(), timespan_t::from_seconds( p()->talents.internal_combustion->effectN( 1 ).base_value() ) );
      timespan_t dot_tick_time = dot->current_action->tick_time( state );
      double ticks_left = remaining / dot_tick_time;
      double total_damage = ticks_left * tick_base_damage;

      action_state_t::release( state );

      base_dd_min = base_dd_max = total_damage;
      warlock_spell_t::execute();
      td( target )->dots_immolate->adjust_duration( -remaining );
    }
  };

  struct chaos_bolt_t : public warlock_spell_t
  {
    struct cry_havoc_t : public warlock_spell_t
    {
      cry_havoc_t( warlock_t* p )
        : warlock_spell_t( "Cry Havoc", p, p->talents.cry_havoc )
      {
        background = dual = true;
        aoe = -1;

        // TOCHECK: Historically, base talent has doubled the sp_coeff
        base_multiplier *= 1.0 + p->talents.cry_havoc->effectN( 1 ).percent();
      }
    };

    internal_combustion_t* internal_combustion;
    cry_havoc_t* cry_havoc;

    chaos_bolt_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Chaos Bolt", p, p->talents.chaos_bolt )
    {
      parse_options( options_str );

      affected_by.chaotic_energies = true;
      affected_by.havoc = true;
      affected_by.chaos_incarnate = p->talents.chaos_incarnate->ok();

      if ( p->talents.chaosbringer.ok() )
        base_dd_multiplier *= 1.0 + p->talents.chaosbringer->effectN( 1 ).percent();

      if ( p->talents.internal_combustion.ok() )
      {
        internal_combustion = new internal_combustion_t( p );
        add_child( internal_combustion );
      }

      if ( p->talents.cry_havoc.ok() )
      {
        cry_havoc = new cry_havoc_t( p );
        add_child( cry_havoc );
      }
    }

    double cost_pct_multiplier() const override
    {
      double c = warlock_spell_t::cost_pct_multiplier();

      if ( p()->buffs.ritual_of_ruin->check() )
        c *= 1.0 + p()->talents.ritual_of_ruin_buff->effectN( 2 ).percent();

      return c;
    }

    timespan_t execute_time() const override
    {
      timespan_t t = warlock_spell_t::execute_time();

      // 2022-10-15: Backdraft is not consumed for Ritual of Ruin empowered casts, but IS hasted by it
      if ( p()->buffs.ritual_of_ruin->check() )
        t *= 1.0 + p()->talents.ritual_of_ruin_buff->effectN( 3 ).percent();
    
      if ( p()->buffs.backdraft->check() )
        t *= 1.0 + p()->talents.backdraft_buff->effectN( 1 ).percent();

      if ( p()->buffs.madness_cb->check() )
        t *= 1.0 + p()->talents.madness_of_the_azjaqir->effectN( 2 ).percent();

      return t;
    }

    double action_multiplier() const override
    {
      double m = warlock_spell_t::action_multiplier();

      if ( p()->talents.madness_of_the_azjaqir.ok() )
        m *= 1.0 + p()->buffs.madness_cb->check_value();

      if ( p()->buffs.crashing_chaos->check() )
        m *= 1.0 + p()->talents.crashing_chaos->effectN( 2 ).percent();

      return m;
    }

    double composite_target_multiplier( player_t* t ) const override
    {
      double m = warlock_spell_t::composite_target_multiplier( t );

      if ( p()->talents.ashen_remains.ok() && td( t )->dots_immolate->is_ticking() )
        m *= 1.0 + p()->talents.ashen_remains->effectN( 1 ).percent();

      return m;
    }

    timespan_t gcd() const override
    {
      timespan_t t = warlock_spell_t::gcd();

      if ( t == 0_ms )
        return t;

      // 2022-10-15: Backdraft is not consumed for Ritual of Ruin empowered casts, but IS hasted by it
      if ( p()->buffs.backdraft->check() )
        t *= 1.0 + p()->talents.backdraft_buff->effectN( 2 ).percent();

      if ( t < min_gcd )
        t = min_gcd;

      return t;
    }

    void impact( action_state_t* s ) override
    {
      warlock_spell_t::impact( s );

      if ( p()->talents.internal_combustion.ok() && result_is_hit( s->result ) && td( s->target )->dots_immolate->is_ticking() )
        internal_combustion->execute_on_target( s->target );

      if ( p()->talents.cry_havoc.ok() && td( s->target )->debuffs_havoc->check() )
        cry_havoc->execute_on_target( s->target );

      if ( p()->talents.eradication.ok() && result_is_hit( s->result ) )
        td( s->target )->debuffs_eradication->trigger();
    }

    void execute() override
    {
      warlock_spell_t::execute();

      // 2022-10-15: Backdraft is not consumed for Ritual of Ruin empowered casts, but IS hasted by it
      if ( !p()->buffs.ritual_of_ruin->check() )
        p()->buffs.backdraft->decrement();

      if ( p()->talents.avatar_of_destruction.ok() && p()->buffs.ritual_of_ruin->check() )
        p()->proc_actions.avatar_of_destruction->execute_on_target( target );

      p()->buffs.ritual_of_ruin->expire();

      p()->buffs.crashing_chaos->decrement();

      if ( p()->talents.madness_of_the_azjaqir.ok() )
        p()->buffs.madness_cb->trigger();

      if ( p()->talents.burn_to_ashes.ok() )
        p()->buffs.burn_to_ashes->trigger( as<int>( p()->talents.burn_to_ashes->effectN( 3 ).base_value() ) );
    }

    double composite_crit_chance() const override
    {
      return 1.0;
    }

    double calculate_direct_amount( action_state_t* s ) const override
    {
      warlock_spell_t::calculate_direct_amount( s );

      s->result_total *= 1.0 + player->cache.spell_crit_chance();

      return s->result_total;
    }
  };

  struct conflagrate_t : public warlock_spell_t
  {
    conflagrate_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Conflagrate", p, p->talents.conflagrate )
    {
      parse_options( options_str );

      affected_by.chaotic_energies = true;
      affected_by.havoc = true;

      energize_type = action_energize::PER_HIT;
      energize_resource = RESOURCE_SOUL_SHARD;
      energize_amount = ( p->talents.conflagrate_2->effectN( 1 ).base_value() ) / 10.0;

      cooldown->hasted = true;
      cooldown->charges += as<int>( p->talents.improved_conflagrate->effectN( 1 ).base_value() );
      cooldown->duration += p->talents.explosive_potential->effectN( 1 ).time_value();

      base_multiplier *= 1.0 + p->talents.ruin->effectN( 1 ).percent();
    }

    void impact( action_state_t* s ) override
    {
      warlock_spell_t::impact( s );

      if ( p()->talents.roaring_blaze.ok() && result_is_hit( s->result ) )
        td( s->target )->debuffs_conflagrate->trigger();
    }

    void execute() override
    {
      warlock_spell_t::execute();

      p()->buffs.conflagration_of_chaos_cf->expire();

      if ( p()->talents.conflagration_of_chaos.ok() && rng().roll( p()->talents.conflagration_of_chaos->effectN( 1 ).percent() ) )
      {
        p()->buffs.conflagration_of_chaos_cf->trigger();
        p()->procs.conflagration_of_chaos_cf->occur();
      }

      if ( p()->talents.backdraft->ok() )
        p()->buffs.backdraft->trigger();

      if ( p()->talents.decimation.ok() && target->health_percentage() <= p()->talents.decimation->effectN( 2 ).base_value() )
        p()->cooldowns.soul_fire->adjust( p()->talents.decimation->effectN( 1 ).time_value() );
    }

    double composite_crit_chance() const override
    {
      if ( p()->buffs.conflagration_of_chaos_cf->check() )
        return 1.0;

      return warlock_spell_t::composite_crit_chance();
    }

    double calculate_direct_amount( action_state_t* s ) const override
    {
      double amt = warlock_spell_t::calculate_direct_amount( s );

      if ( p()->buffs.conflagration_of_chaos_cf->check() )
      {
        s->result_total *= 1.0 + player->cache.spell_crit_chance();
        return s->result_total;
      }

      return amt;
    }
  };

  struct rain_of_fire_t : public warlock_spell_t
  {
    struct rain_of_fire_tick_t : public warlock_spell_t
    {
      rain_of_fire_tick_t( warlock_t* p )
        : warlock_spell_t( "Rain of Fire (tick)", p, p->talents.rain_of_fire_tick )
      {
        background = dual = true;
        aoe = -1;
        radius = p->talents.rain_of_fire->effectN( 1 ).radius();

        affected_by.chaotic_energies = true;
        affected_by.chaos_incarnate = p->talents.chaos_incarnate.ok();

        base_multiplier *= 1.0 + p->talents.inferno->effectN( 2 ).percent();
        base_multiplier *= 1.0 + p->talents.chaosbringer->effectN( 2 ).percent();
      }
      
      void impact( action_state_t* s ) override
      {
        warlock_spell_t::impact( s );

        if ( p()->talents.inferno.ok() && result_is_hit( s->result ) )
        {
          if ( rng().roll( p()->talents.inferno->effectN( 1 ).percent() ) )
            p()->resource_gain( RESOURCE_SOUL_SHARD, 0.1, p()->gains.inferno );
        }

        if ( p()->talents.pyrogenics.ok() )
          td( s->target )->debuffs_pyrogenics->trigger();
      }

      double composite_persistent_multiplier( const action_state_t* s ) const override
      {
        double m = warlock_spell_t::composite_persistent_multiplier( s );

        if ( p()->buffs.crashing_chaos->check() )
          m *= 1.0 + p()->talents.crashing_chaos->effectN( 1 ).percent();

        return m;
      }

      double action_multiplier() const override
      {
        double m = warlock_spell_t::action_multiplier();

        if ( p()->buffs.madness_rof_snapshot->check() )
          m *= 1.0 + p()->talents.madness_of_the_azjaqir->effectN( 1 ).percent();

        return m;
      }
    };

    rain_of_fire_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Rain of Fire", p, p->talents.rain_of_fire )
    {
      parse_options( options_str );
      
      may_miss = may_crit = false;
      base_tick_time = 1_s;
      dot_duration = 0_s;
      aoe = -1; // Needed to apply Pyrogenics

      if ( !p->proc_actions.rain_of_fire_tick )
      {
        p->proc_actions.rain_of_fire_tick = new rain_of_fire_tick_t( p );
        p->proc_actions.rain_of_fire_tick->stats = stats;
      }
    }

    double cost_pct_multiplier() const override
    {
      double c = warlock_spell_t::cost_pct_multiplier();

      if ( p()->buffs.ritual_of_ruin->check() )
        c *= 1.0 + p()->talents.ritual_of_ruin_buff->effectN( 5 ).percent();

      return c;
    }

    void execute() override
    {
      warlock_spell_t::execute();

      p()->buffs.madness_rof_snapshot->expire();

      if ( p()->buffs.madness_rof->check() )
        p()->buffs.madness_rof_snapshot->trigger();

      if ( p()->talents.madness_of_the_azjaqir.ok() )
        p()->buffs.madness_rof->trigger();

      if ( p()->talents.burn_to_ashes.ok() )
        p()->buffs.burn_to_ashes->trigger( as<int>( p()->talents.burn_to_ashes->effectN( 3 ).base_value() ) );

      make_event<ground_aoe_event_t>( *sim, p(), 
                                      ground_aoe_params_t()
                                        .target( execute_state->target )
                                        .x( execute_state->target->x_position )
                                        .y( execute_state->target->y_position )
                                        .pulse_time( base_tick_time * player->cache.spell_haste() )
                                        .duration( p()->talents.rain_of_fire->duration() * player->cache.spell_haste() )
                                        .start_time( sim->current_time() )
                                        .action( p()->proc_actions.rain_of_fire_tick ) );

      if ( p()->talents.avatar_of_destruction.ok() && p()->buffs.ritual_of_ruin->check() )
        p()->proc_actions.avatar_of_destruction->execute_on_target( target );

      p()->buffs.ritual_of_ruin->expire();

      p()->buffs.crashing_chaos->decrement();
    }

    void impact( action_state_t* s ) override
    {
      warlock_spell_t::impact( s );

      if ( p()->talents.pyrogenics.ok() )
        td( s->target )->debuffs_pyrogenics->trigger();
    }
  };

  struct havoc_t : public warlock_spell_t
  {
    havoc_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Havoc", p, p->talents.havoc )
    {
      parse_options( options_str );

      may_crit = false;
    }

    void impact( action_state_t* s ) override
    {
      warlock_spell_t::impact( s );

      td( s->target )->debuffs_havoc->trigger();
    }
  };

  struct cataclysm_t : public warlock_spell_t
  {
    immolate_t* immolate;

    cataclysm_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Cataclysm", p, p->talents.cataclysm ),
      immolate( new immolate_t( p, "" ) )
    {
      parse_options( options_str );

      aoe = -1;

      affected_by.chaotic_energies = true;

      immolate->background = true;
      immolate->dual = true;
      immolate->base_costs[ RESOURCE_MANA ] = 0;
      immolate->base_dd_multiplier = 0.0;
    }

    void impact( action_state_t* s ) override
    {
      warlock_spell_t::impact( s );

      if ( result_is_hit( s->result ) )
        immolate->execute_on_target( s->target );
    }
  };

  struct shadowburn_t : public warlock_spell_t
  {
    shadowburn_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Shadowburn", p, p->talents.shadowburn )
    {
      parse_options( options_str );

      cooldown->hasted = true;
      
      affected_by.chaotic_energies = true;
      affected_by.havoc = true;
      affected_by.chaos_incarnate = p->talents.chaos_incarnate.ok();

      base_multiplier *= 1.0 + p->talents.ruin->effectN( 1 ).percent();

      if ( p->talents.chaosbringer.ok() )
        base_multiplier *= 1.0 + p->talents.chaosbringer->effectN( 3 ).percent();
    }

    void impact( action_state_t* s ) override
    {
      warlock_spell_t::impact( s );

      if ( result_is_hit( s->result ) )
      {
        td( s->target )->debuffs_shadowburn->trigger();

        if ( p()->talents.eradication.ok() )
          td( s->target )->debuffs_eradication->trigger();
      }
    }

    void execute() override
    {
      warlock_spell_t::execute();

      p()->buffs.conflagration_of_chaos_sb->expire();

      if ( p()->talents.conflagration_of_chaos.ok() && rng().roll( p()->talents.conflagration_of_chaos->effectN( 1 ).percent() ) )
      {
        p()->buffs.conflagration_of_chaos_sb->trigger();
        p()->procs.conflagration_of_chaos_sb->occur();
      }

      if ( p()->talents.madness_of_the_azjaqir.ok() )
        p()->buffs.madness_sb->trigger();

      if ( p()->talents.burn_to_ashes.ok() )
        p()->buffs.burn_to_ashes->trigger( as<int>( p()->talents.burn_to_ashes->effectN( 4 ).base_value() ) );
    }

    double action_multiplier() const override
    {
      double m = warlock_spell_t::action_multiplier();

      if ( p()->talents.madness_of_the_azjaqir.ok() )
        m *= 1.0 + p()->buffs.madness_sb->check_value();

      return m;
    }

    double composite_target_multiplier( player_t* t ) const override
    {
      double m = warlock_spell_t::composite_target_multiplier( t );

      if ( p()->talents.ashen_remains.ok() && td( t )->dots_immolate->is_ticking() )
        m *= 1.0 + p()->talents.ashen_remains->effectN( 1 ).percent();

      return m;
    }

    double composite_target_crit_chance( player_t* t ) const override
    {
      double m = warlock_spell_t::composite_target_crit_chance( t );

      if ( target->health_percentage() <= 20.0 )
        m += p()->talents.shadowburn->effectN( 3 ).percent();

      return m;
    }

    double composite_crit_chance() const override
    {
      if ( p()->buffs.conflagration_of_chaos_sb->check() )
        return 1.0;

      return warlock_spell_t::composite_crit_chance();
    }

    double calculate_direct_amount( action_state_t* state ) const override
    {
      double amt = warlock_spell_t::calculate_direct_amount( state );

      if ( p()->buffs.conflagration_of_chaos_sb->check() )
      {
        state->result_total *= 1.0 + player->cache.spell_crit_chance();
        return state->result_total;
      }

      return amt;
    }
  };

  struct channel_demonfire_t : public warlock_spell_t
  {
    struct channel_demonfire_tick_t : public warlock_spell_t
    {
      channel_demonfire_tick_t( warlock_t* p )
        : warlock_spell_t( "Channel Demonfire (tick)", p, p->talents.channel_demonfire_tick )
      {
        background = dual = true;
        may_miss = false;
        aoe = -1;
        travel_speed = p->talents.channel_demonfire_travel->missile_speed();

        affected_by.chaotic_energies = true;

        // TOCHECK: Is this needed?
        spell_power_mod.direct = p->talents.channel_demonfire_tick->effectN( 1 ).sp_coeff();
      }

      void impact( action_state_t* s ) override
      {
        warlock_spell_t::impact( s );

        if ( p()->talents.raging_demonfire.ok() && td( s->target )->dots_immolate->is_ticking() )
          td( s->target )->dots_immolate->adjust_duration( p()->talents.raging_demonfire->effectN( 2 ).time_value() );

        if ( s->chain_target == 0 && p()->sets->has_set_bonus( WARLOCK_DESTRUCTION, T30, B2 ) )
        {
          double increment_max = 0.4;

          p()->cdf_accumulator += rng().range( 0.0, increment_max );

          if ( p()->cdf_accumulator >= 1.0 )
          {
            p()->proc_actions.channel_demonfire->execute_on_target( s->target );
            p()->procs.channel_demonfire->occur();
            p()->cdf_accumulator -= 1.0;
          }

          if ( p()->sets->has_set_bonus( WARLOCK_DESTRUCTION, T30, B4 ) )
            p()->buffs.umbrafire_embers->trigger();
        }
      }
    };

    channel_demonfire_tick_t* channel_demonfire_tick;

    channel_demonfire_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Channel Demonfire", p, p->talents.channel_demonfire ),
      channel_demonfire_tick( new channel_demonfire_tick_t( p ) )
    {
      parse_options( options_str );
      
      channeled = true;
      hasted_ticks = true;
      may_crit = false;

      add_child( channel_demonfire_tick );

      if ( p->talents.raging_demonfire.ok() )
      {
        int num_ticks = as<int>( dot_duration / base_tick_time + p->talents.raging_demonfire->effectN( 1 ).base_value() );
        base_tick_time *= 1.0 + p->talents.raging_demonfire->effectN( 3 ).percent();
        dot_duration = num_ticks * base_tick_time;
      }
    }

    void init() override
    {
      warlock_spell_t::init();

      cooldown->hasted = true;
    }

    std::vector<player_t*>& target_list() const override
    {
      target_cache.list = warlock_spell_t::target_list();

      size_t i = target_cache.list.size();
      while ( i > 0 )
      {
        i--;

        if ( !td( target_cache.list[ i ] )->dots_immolate->is_ticking() )
          target_cache.list.erase( target_cache.list.begin() + i );
      }

      return target_cache.list;
    }

    void execute() override
    {
      warlock_spell_t::execute();

      if ( p()->buffs.umbrafire_embers->check() )
      {
        timespan_t base = p()->buffs.umbrafire_embers->base_buff_duration;
        timespan_t remains = p()->buffs.umbrafire_embers->remains();
        p()->buffs.umbrafire_embers->extend_duration( p(), base - remains );
      }
    }

    void tick( dot_t* d ) override
    {
      target_cache.is_valid = false;

      const auto& targets = target_list();

      if ( !targets.empty() )
      {
        channel_demonfire_tick->set_target( targets[ rng().range( size_t(), targets.size() ) ] );
        channel_demonfire_tick->execute();
      }

      warlock_spell_t::tick( d );
    }

    bool ready() override
    {
      if ( p()->get_active_dots( td( target )->dots_immolate ) == 0 )
        return false;

      return warlock_spell_t::ready();
    }
  };

  struct dimensional_cinder_t : public warlock_spell_t
  {
    dimensional_cinder_t( warlock_t* p )
      : warlock_spell_t( "Dimensional Cinder", p, p->tier.dimensional_cinder )
    {
      background = dual = true;
      may_crit = false;
      aoe = -1;

      base_dd_min = base_dd_max = 0.0;
    }
  };

  struct shadowy_tear_t : public warlock_spell_t
  {
    struct rift_shadow_bolt_t : public warlock_spell_t
    {
      dimensional_cinder_t* cinder;

      rift_shadow_bolt_t( warlock_t* p )
        : warlock_spell_t( "Rift Shadow Bolt", p, p->talents.rift_shadow_bolt )
      {
        background = dual = true;

        // TOCHECK: In the past, this double-dipped on both direct and periodic spell auras
        base_dd_multiplier *= 1.0 + p->warlock_base.destruction_warlock->effectN( 2 ).percent();

        cinder = new dimensional_cinder_t( p );
      }

      void impact( action_state_t* s ) override
      {
        warlock_spell_t::impact( s );

        double raw_damage = s->result_total;

        if ( p()->sets->has_set_bonus( WARLOCK_DESTRUCTION, T31, B2 ) )
        {
          cinder->base_dd_min = cinder->base_dd_max = raw_damage * p()->sets->set( WARLOCK_DESTRUCTION, T31, B2 )->effectN( 1 ).percent();
          cinder->execute_on_target( s->target );
        }
      }
    };

    struct shadow_barrage_t : public warlock_spell_t
    {
      shadow_barrage_t( warlock_t* p )
        : warlock_spell_t( "Shadow Barrage", p, p->talents.shadow_barrage )
      {
        background = true;

        tick_action = new rift_shadow_bolt_t( p );
      }

      double last_tick_factor( const dot_t* d, timespan_t t, timespan_t dur ) const override
      { return 1.0; }
    };

    shadowy_tear_t( warlock_t* p )
      : warlock_spell_t( "Shadowy Tear", p, p->talents.shadowy_tear_summon )
    {
      background = true;

      impact_action = new shadow_barrage_t( p );
    }
  };

  struct unstable_tear_t : public warlock_spell_t
  {
    struct chaos_barrage_tick_t : public warlock_spell_t
    {
      dimensional_cinder_t* cinder;

      chaos_barrage_tick_t( warlock_t* p )
        : warlock_spell_t( "Chaos Barrage (tick)", p, p->talents.chaos_barrage_tick )
      {
        background = dual = true;

        base_dd_multiplier *= 1.0 + p->warlock_base.destruction_warlock->effectN( 2 ).percent();

        cinder = new dimensional_cinder_t( p );
      }

      void impact( action_state_t* s ) override
      {
        warlock_spell_t::impact( s );

        double raw_damage = s->result_total;

        if ( p()->sets->has_set_bonus( WARLOCK_DESTRUCTION, T31, B2 ) )
        {
          cinder->base_dd_min = cinder->base_dd_max = raw_damage * p()->sets->set( WARLOCK_DESTRUCTION, T31, B2 )->effectN( 1 ).percent();
          cinder->execute_on_target( s->target );
        }
      }
    };

    struct chaos_barrage_t : public warlock_spell_t
    {
      chaos_barrage_t( warlock_t* p )
        : warlock_spell_t( "Chaos Barrage", p, p->talents.chaos_barrage )
      {
        background = true;

        tick_action = new chaos_barrage_tick_t( p );
      }
    };

    unstable_tear_t( warlock_t* p )
      : warlock_spell_t( "Unstable Tear", p, p->talents.unstable_tear_summon )
    {
      background = true;

      impact_action = new chaos_barrage_t( p );
    }
  };

  struct chaos_tear_t : public warlock_spell_t
  {
    struct rift_chaos_bolt_t : public warlock_spell_t
    {
      dimensional_cinder_t* cinder;

      rift_chaos_bolt_t( warlock_t* p )
        : warlock_spell_t( "Rift Chaos Bolt", p, p->talents.rift_chaos_bolt )
      {
        background = true;

        base_dd_multiplier *= 1.0 + p->warlock_base.destruction_warlock->effectN( 2 ).percent();

        cinder = new dimensional_cinder_t( p );
      }

      double composite_crit_chance() const override
      { return 1.0; }

      double calculate_direct_amount( action_state_t* s ) const override
      {
        warlock_spell_t::calculate_direct_amount( s );

        s->result_total *= 1.0 + player->cache.spell_crit_chance();

        return s->result_total;
      }

      void impact( action_state_t* s ) override
      {
        warlock_spell_t::impact( s );

        double raw_damage = s->result_total;

        if ( p()->sets->has_set_bonus( WARLOCK_DESTRUCTION, T31, B2 ) )
        {
          cinder->base_dd_min = cinder->base_dd_max = raw_damage * p()->sets->set( WARLOCK_DESTRUCTION, T31, B2 )->effectN( 1 ).percent();
          cinder->execute_on_target( s->target );
        }
      }
    };

    chaos_tear_t( warlock_t* p )
      : warlock_spell_t( "Chaos Tear", p, p->talents.chaos_tear_summon )
    {
      background = true;
      
      impact_action = new rift_chaos_bolt_t( p );
    }
  };

  struct flame_rift_t : public warlock_spell_t
  {
    struct searing_bolt_t : public warlock_spell_t
    {
      dimensional_cinder_t* cinder;

      searing_bolt_t( warlock_t* p )
        : warlock_spell_t( "Searing Bolt", p, p->tier.searing_bolt )
      {
        background = true;
        dot_behavior = dot_behavior_e::DOT_REFRESH_DURATION;

        base_dd_multiplier *= 1.0 + p->warlock_base.destruction_warlock->effectN( 2 ).percent();

        cinder = new dimensional_cinder_t( p );
      }

      void impact( action_state_t* s ) override
      {
        warlock_spell_t::impact( s );

        double raw_damage = s->result_total;

        if ( p()->sets->has_set_bonus( WARLOCK_DESTRUCTION, T31, B2 ) )
        {
          cinder->base_dd_min = cinder->base_dd_max = raw_damage * p()->sets->set( WARLOCK_DESTRUCTION, T31, B2 )->effectN( 1 ).percent();
          cinder->execute_on_target( s->target );
        }
      }
    };

    searing_bolt_t* bolt;

    flame_rift_t( warlock_t* p )
      : warlock_spell_t( "Flame Rift", p, p->tier.flame_rift )
    {
      background = true;
      may_miss = false;

      bolt = new searing_bolt_t( p );
      add_child( bolt );
    }

    void execute() override
    {
      warlock_spell_t::execute();

      timespan_t min_delay = 0_ms;
      timespan_t max_delay = timespan_t::from_seconds( p()->sets->set( WARLOCK_DESTRUCTION, T31, B4 )->effectN( 1 ).base_value() );
      player_t* tar = target;

      for ( int i = 0; i < p()->sets->set( WARLOCK_DESTRUCTION, T31, B4 )->effectN( 1 ).base_value(); i++ )
      {
        timespan_t delay = rng().gauss( i * 500_ms, 500_ms );
        delay = std::min( std::max( delay, min_delay ), max_delay );
        make_event( *sim, delay, [ this, tar ]{ this->bolt->execute_on_target( tar ); } );
      }
    }
  };

  struct dimensional_rift_t : public warlock_spell_t
  {
    shadowy_tear_t* shadowy_tear;
    unstable_tear_t* unstable_tear;
    chaos_tear_t* chaos_tear;
    flame_rift_t* flame_rift;

    dimensional_rift_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Dimensional Rift", p, p->talents.dimensional_rift )
    {
      parse_options( options_str );
      
      harmful = true;

      energize_type = action_energize::ON_CAST;
      energize_amount = p->talents.dimensional_rift->effectN( 2 ).base_value() / 10.0;

      shadowy_tear = new shadowy_tear_t( p );
      unstable_tear = new unstable_tear_t( p );
      chaos_tear = new chaos_tear_t( p );
      flame_rift = new flame_rift_t( p );

      add_child( shadowy_tear );
      add_child( unstable_tear );
      add_child( chaos_tear );
      add_child( flame_rift );
    }

    void execute() override
    {
      warlock_spell_t::execute();

      int rift = rng().range( p()->sets->has_set_bonus( WARLOCK_DESTRUCTION, T31, B4 ) ? 4 : 3 );

      switch ( rift )
      {
      case 0:
        shadowy_tear->execute_on_target( target );
        break;
      case 1:
        unstable_tear->execute_on_target( target );
        break;
      case 2:
        chaos_tear->execute_on_target( target );
        break;
      case 3:
        flame_rift->execute_on_target( target );
        break;
      default:
        break;
      }
    }
  };

  struct infernal_awakening_t : public warlock_spell_t
  {
    infernal_awakening_t( warlock_t* p )
      : warlock_spell_t( "Infernal Awakening", p, p->talents.infernal_awakening )
    {
      background = dual = true;
      aoe = -1;
    }

    void execute() override
    {
      warlock_spell_t::execute();
      
      p()->warlock_pet_list.infernals.spawn( p()->talents.summon_infernal_main->duration() );
    }
  };

  struct summon_infernal_t : public warlock_spell_t
  {
    summon_infernal_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Summon Infernal", p, p->talents.summon_infernal )
    {
      parse_options( options_str );

      may_crit = false;
      impact_action = new infernal_awakening_t( p );
      add_child( impact_action );
    }

    void execute() override
    {
      warlock_spell_t::execute();

      if ( p()->talents.crashing_chaos.ok() )
        p()->buffs.crashing_chaos->trigger();

      if ( p()->talents.rain_of_chaos.ok() )
        p()->buffs.rain_of_chaos->trigger();
    }
  };

  struct soul_fire_t : public warlock_spell_t
  {
    immolate_t* immolate;

    soul_fire_t( warlock_t* p, util::string_view options_str )
      : warlock_spell_t( "Soul Fire", p, p->talents.soul_fire ),
      immolate( new immolate_t( p, "" ) )
    {
      parse_options( options_str );

      energize_type = action_energize::PER_HIT;
      energize_resource = RESOURCE_SOUL_SHARD;
      energize_amount = ( p->talents.soul_fire_2->effectN( 1 ).base_value() ) / 10.0;

      affected_by.chaotic_energies = true;
      affected_by.havoc = true;

      base_multiplier *= 1.0 + p->talents.ruin->effectN( 1 ).percent();

      immolate->background = true;
      immolate->dual = true;
      immolate->base_costs[ RESOURCE_MANA ] = 0;
      immolate->base_dd_multiplier = 0.0;
    }

    timespan_t execute_time() const override
    {
      timespan_t t = warlock_spell_t::execute_time();

      if ( p()->buffs.backdraft->check() )
        t *= 1.0 + p()->talents.backdraft_buff->effectN( 1 ).percent();

      return t;
    }

    timespan_t gcd() const override
    {
      timespan_t t = warlock_spell_t::gcd();

      if ( t == 0_ms )
        return t;

      if ( p()->buffs.backdraft->check() )
        t *= 1.0 + p()->talents.backdraft_buff->effectN( 2 ).percent();

      if ( t < min_gcd )
        t = min_gcd;

      return t;
    }

    void execute() override
    {
      warlock_spell_t::execute();

      immolate->execute_on_target( target );

      p()->buffs.backdraft->decrement();
    }
  };

  struct avatar_of_destruction_t : public warlock_spell_t
  {
    struct infernal_awakening_proc_t : public warlock_spell_t
    {
      infernal_awakening_proc_t( warlock_t* p )
        : warlock_spell_t( "Infernal Awakening (Blasphemy)", p, p->talents.infernal_awakening )
      {
        background = dual = true;
        aoe = -1;
      }
    };

    infernal_awakening_proc_t* infernal_awakening;

    avatar_of_destruction_t( warlock_t* p )
      : warlock_spell_t( "Avatar of Destruction", p, p->talents.summon_blasphemy )
    {
      background = dual = true;
      infernal_awakening = new infernal_awakening_proc_t( p );
    }

    void execute() override
    {
      warlock_spell_t::execute();

      if ( p()->warlock_pet_list.blasphemy.active_pet() )
      {
        p()->warlock_pet_list.blasphemy.active_pet()->adjust_duration( p()->talents.avatar_of_destruction->effectN( 1 ).time_value() * 1000 );
        p()->warlock_pet_list.blasphemy.active_pet()->blasphemous_existence->execute();
      }
      else
      {
        p()->warlock_pet_list.blasphemy.spawn( p()->talents.avatar_of_destruction->effectN( 1 ).time_value() * 1000 );
        infernal_awakening->execute_on_target( target );
      }
    }
  };

  struct channel_demonfire_tier_t : public warlock_spell_t
  {
    channel_demonfire_tier_t( warlock_t* p )
      : warlock_spell_t( "Channel Demonfire (Tier)", p, p->tier.channel_demonfire )
    {
      background = dual = false;
      aoe = -1;
      travel_speed = p->talents.channel_demonfire_travel->missile_speed();
      
      affected_by.chaotic_energies = true;

      spell_power_mod.direct = p->tier.channel_demonfire->effectN( 1 ).sp_coeff();
      base_aoe_multiplier = p->tier.channel_demonfire->effectN( 2 ).sp_coeff() / p->tier.channel_demonfire->effectN( 1 ).sp_coeff();
    }

    void impact( action_state_t* s ) override
    {
      warlock_spell_t::impact( s );

      if ( p()->talents.raging_demonfire.ok() && td( s->target )->dots_immolate->is_ticking() )
        td( s->target )->dots_immolate->adjust_duration( p()->talents.raging_demonfire->effectN( 2 ).time_value() );

      if ( s->chain_target == 0 && p()->sets->has_set_bonus( WARLOCK_DESTRUCTION, T30, B4 ) )
        p()->buffs.umbrafire_embers->trigger();
    }
  };

  // Destruction Actions End
  // Helper Functions Begin

  // Event for triggering delayed refunds from Soul Conduit
  // Delay prevents instant reaction time issues for rng refunds
  sc_event_t::sc_event_t( warlock_t* p, int c )
    : player_event_t( *p, 100_ms ),
    shard_gain( p->gains.soul_conduit ),
    pl( p ),
    shards_used( c )
  { }

  const char* sc_event_t::name() const
  { return "soul_conduit_event"; }

  void sc_event_t::execute()
  {
    double soul_conduit_rng = pl->talents.soul_conduit->effectN( 1 ).percent();

    for ( int i = 0; i < shards_used; i++ )
    {
      if ( rng().roll( soul_conduit_rng ) )
      {
        pl->sim->print_log( "Soul Conduit proc occurred for Warlock {}, refunding 1.0 soul shards.", pl->name() );
        pl->resource_gain( RESOURCE_SOUL_SHARD, 1.0, shard_gain );
        pl->procs.soul_conduit->occur();
      }
    }
  }

  // Checks whether Tormented Crescendo conditions are met
  bool helpers::crescendo_check( warlock_t* p )
  {
    bool agony = false;
    bool corruption = false;
    for ( const auto target : p->sim->target_non_sleeping_list )
    {
      warlock_td_t* td = p->get_target_data( target );
      if ( !td )
        continue;

      agony = agony || td->dots_agony->is_ticking();
      corruption = corruption || td->dots_corruption->is_ticking();

      if ( agony && corruption )
        break;
    }

    return agony && corruption && ( p->ua_target && p->get_target_data( p->ua_target )->dots_unstable_affliction->is_ticking() );
  }

  // Event for spawning Wild Imps for Demonology
  imp_delay_event_t::imp_delay_event_t( warlock_t* p, double delay, double exp ) : player_event_t( *p, timespan_t::from_millis( delay ) )
  { diff = timespan_t::from_millis( exp - delay ); }

  const char* imp_delay_event_t::name() const
  { return "imp_delay"; }

  void imp_delay_event_t::execute()
  {
    warlock_t* p = static_cast<warlock_t*>( player() );

    p->warlock_pet_list.wild_imps.spawn();

    // Remove this event from the vector
    auto it = std::find( p->wild_imp_spawns.begin(), p->wild_imp_spawns.end(), this );
    if ( it != p->wild_imp_spawns.end() )
      p->wild_imp_spawns.erase( it );
  }

  // Used for APL expressions to estimate when imp is "supposed" to spawn
  timespan_t imp_delay_event_t::expected_time()
  { return std::max( 0_ms, this->remains() + diff ); }

  // Helper Functions End
  
  // Action Creation Begin

  action_t* warlock_t::create_action( util::string_view action_name, util::string_view options_str )
  {
    if ( specialization() == WARLOCK_AFFLICTION )
    {
      if ( action_t* aff_action = create_action_affliction( action_name, options_str ) )
        return aff_action;
    }

    if ( specialization() == WARLOCK_DEMONOLOGY )
    {
      if ( action_t* demo_action = create_action_demonology( action_name, options_str ) )
        return demo_action;
    }

    if ( specialization() == WARLOCK_DESTRUCTION )
    {
      if ( action_t* destro_action = create_action_destruction( action_name, options_str ) )
        return destro_action;
    }

    if ( action_t* generic_action = create_action_warlock( action_name, options_str ) )
      return generic_action;

    return player_t::create_action( action_name, options_str );
  }

  action_t* warlock_t::create_action_warlock( util::string_view action_name, util::string_view options_str )
  {
    if ( ( action_name == "summon_pet" ) && default_pet.empty() )
    {
      sim->errorf( "Player %s used a generic pet summoning action without specifying a default_pet.\n", name() );
      return nullptr;
    }

    // Pets
    if ( action_name == "summon_felhunter" )
      return new summon_main_pet_t( "felhunter", this );
    if ( action_name == "summon_felguard" && specialization() == WARLOCK_DEMONOLOGY )
      return new summon_main_pet_t( "felguard", this );
    if ( action_name == "summon_sayaad" )
      return new summon_main_pet_t( "sayaad", this, 366222 );
    if ( action_name == "summon_succubus" )
      return new summon_main_pet_t( "succubus", this, 366222 );
    if ( action_name  == "summon_incubus" )
      return new summon_main_pet_t( "incubus", this, 366222 );
    if ( action_name == "summon_voidwalker" )
      return new summon_main_pet_t( "voidwalker", this );
    if ( action_name == "summon_imp" )
      return new summon_main_pet_t( "imp", this );
    if ( action_name == "summon_pet" )
    {
      if ( default_pet == "sayaad" || default_pet == "succubus" || default_pet == "incubus" )
        return new summon_main_pet_t( default_pet, this, 366222 );

      return new summon_main_pet_t( default_pet, this );
    }

    // Shared Spells
    if ( action_name == "drain_life" )
      return new drain_life_t( this, options_str );
    if ( action_name == "corruption" && specialization() != WARLOCK_DESTRUCTION )
      return new corruption_t( this, options_str, false );
    if ( action_name == "shadow_bolt" && specialization() != WARLOCK_DESTRUCTION )
      return new shadow_bolt_t( this, options_str );
    if ( action_name == "grimoire_of_sacrifice" )
      return new grimoire_of_sacrifice_t( this, options_str );  // aff and destro
    if ( action_name == "interrupt" )
      return new interrupt_t( action_name, this, options_str );
    if ( action_name == "soulburn" )
      return new soulburn_t( this, options_str );

    return nullptr;
  }

  action_t* warlock_t::create_action_affliction( util::string_view action_name, util::string_view options_str )
  {
    if ( action_name == "agony" )
      return new agony_t( this, options_str );
    if ( action_name == "unstable_affliction" )
      return new unstable_affliction_t( this, options_str );
    if ( action_name == "summon_darkglare" )
      return new summon_darkglare_t( this, options_str );
    if ( action_name == "drain_soul" )
      return new drain_soul_t( this, options_str );
    if ( action_name == "haunt" )
      return new haunt_t( this, options_str );
    if ( action_name == "phantom_singularity" )
      return new phantom_singularity_t( this, options_str );
    if ( action_name == "siphon_life" )
      return new siphon_life_t( this, options_str );
    if ( action_name == "vile_taint" )
      return new vile_taint_t( this, options_str );
    if ( action_name == "malefic_rapture" )
      return new malefic_rapture_t( this, options_str );
    if ( action_name == "soul_rot" )
      return new soul_rot_t( this, options_str );
    if ( action_name == "seed_of_corruption" )
      return new seed_of_corruption_t( this, options_str );

    return nullptr;
  }

  action_t* warlock_t::create_action_demonology( util::string_view action_name, util::string_view options_str )
  {
    if ( action_name == "demonbolt" )
      return new demonbolt_t( this, options_str );
    if ( action_name == "hand_of_guldan" )
      return new hand_of_guldan_t( this, options_str );
    if ( action_name == "implosion" )
      return new implosion_t( this, options_str );
    if ( action_name == "demonic_strength" )
      return new demonic_strength_t( this, options_str );
    if ( action_name == "bilescourge_bombers" )
      return new bilescourge_bombers_t( this, options_str );
    if ( action_name == "doom" )
      return new doom_t( this, options_str );
    if ( action_name == "power_siphon" )
      return new power_siphon_t( this, options_str );
    if ( action_name == "call_dreadstalkers" )
      return new call_dreadstalkers_t( this, options_str );
    if ( action_name == "summon_demonic_tyrant" )
      return new summon_demonic_tyrant_t( this, options_str );
    if ( action_name == "summon_vilefiend" )
      return new summon_vilefiend_t( this, options_str );
    if ( action_name == "grimoire_felguard" )
      return new grimoire_felguard_t( this, options_str );
    if ( action_name == "guillotine" )
      return new guillotine_t( this, options_str );

    return nullptr;
  }

  action_t* warlock_t::create_action_destruction( util::string_view action_name, util::string_view options_str )
  {
    if ( action_name == "conflagrate" )
      return new conflagrate_t( this, options_str );
    if ( action_name == "incinerate" )
      return new incinerate_t( this, options_str );
    if ( action_name == "immolate" )
      return new immolate_t( this, options_str );
    if ( action_name == "chaos_bolt" )
      return new chaos_bolt_t( this, options_str );
    if ( action_name == "rain_of_fire" )
      return new rain_of_fire_t( this, options_str );
    if ( action_name == "havoc" )
      return new havoc_t( this, options_str );
    if ( action_name == "summon_infernal" )
      return new summon_infernal_t( this, options_str );
    if ( action_name == "soul_fire" )
      return new soul_fire_t( this, options_str );
    if ( action_name == "shadowburn" )
      return new shadowburn_t( this, options_str );
    if ( action_name == "cataclysm" )
      return new cataclysm_t( this, options_str );
    if ( action_name == "channel_demonfire" )
      return new channel_demonfire_t( this, options_str );
    if ( action_name == "dimensional_rift" )
      return new dimensional_rift_t( this, options_str );

    return nullptr;
  }

  void warlock_t::create_actions()
  {
    if ( specialization() == WARLOCK_AFFLICTION )
      create_affliction_proc_actions();

    if ( specialization() == WARLOCK_DEMONOLOGY )
      create_demonology_proc_actions();

    if ( specialization() == WARLOCK_DESTRUCTION )
      create_destruction_proc_actions();

    player_t::create_actions();
  }

  void warlock_t::create_affliction_proc_actions()
  {
    proc_actions.soul_flame_proc = new soul_flame_t( this );
    proc_actions.pandemic_invocation_proc = new pandemic_invocation_t( this );
  }

  void warlock_t::create_demonology_proc_actions()
  {
    proc_actions.bilescourge_bombers_proc = new bilescourge_bombers_proc_t( this );
    proc_actions.doom_brand_explosion = new doom_brand_t( this );
  }

  void warlock_t::create_destruction_proc_actions()
  {
    proc_actions.avatar_of_destruction = new avatar_of_destruction_t( this );
    proc_actions.channel_demonfire = new channel_demonfire_tier_t( this );
  }

  void warlock_t::init_special_effects()
  {
    player_t::init_special_effects();

    if ( talents.grimoire_of_sacrifice.ok() )
    {
      auto const sac_effect = new special_effect_t( this );
      sac_effect->name_str = "grimoire_of_sacrifice_effect";
      sac_effect->spell_id = talents.grimoire_of_sacrifice_buff->id();
      sac_effect->execute_action = new grimoire_of_sacrifice_damage_t( this );
      special_effects.push_back( sac_effect );

      auto cb = new dbc_proc_callback_t( this, *sac_effect );

      cb->initialize();
      cb->deactivate();

      buffs.grimoire_of_sacrifice->set_stack_change_callback( [ cb ]( buff_t*, int, int new_ ){
          if ( new_ == 1 ) cb->activate();
          else cb->deactivate();
        } );
    }
  }

  // Action Creation End

} //namespace warlock