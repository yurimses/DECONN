/*
 * Copyright (c) 2010, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This file is part of the Contiki operating system.
 *
 */

/**
 * \file
 *         The Minimum Rank with Hysteresis Objective Function (MRHOF), RFC6719
 *
 *         This implementation uses the estimated number of
 *         transmissions (ETX) as the additive routing metric,
 *         and also provides stubs for the energy metric.
 *
 * \author Joakim Eriksson <joakime@sics.se>, Nicolas Tsiftes <nvt@sics.se>
 */

/**
 * \addtogroup uip6
 * @{
 */

#include "net/rpl/rpl.h"
#include "net/rpl/rpl-private.h"
#include "net/nbr-table.h"
#include "net/link-stats.h"
#include "sys/battery_kinetic.h" //riker


#define DEBUG 0
#include "net/ip/uip-debug.h"
#include <stdio.h>




/* RFC6551 and RFC6719 do not mandate the use of a specific formula to
 * compute the ETX value. This MRHOF implementation relies on the value
 * computed by the link-stats module. It has an optional feature,
 * RPL_MRHOF_CONF_SQUARED_ETX, that consists in squaring this value.
 * This basically penalizes bad links while preserving the semantics of ETX
 * (1 = perfect link, more = worse link). As a result, MRHOF will favor
 * good links over short paths. Recommended when reliability is a priority.
 * Without this feature, a hop with 50% PRR (ETX=2) is equivalent to two
 * perfect hops with 100% PRR (ETX=1+1=2). With this feature, the former
 * path obtains ETX=2*2=4 and the former ETX=1*1+1*1=2. */
#ifdef RPL_MRHOF_CONF_SQUARED_ETX
#define RPL_MRHOF_SQUARED_ETX RPL_MRHOF_CONF_SQUARED_ETX
#else /* RPL_MRHOF_CONF_SQUARED_ETX */
#define RPL_MRHOF_SQUARED_ETX 0
#endif /* RPL_MRHOF_CONF_SQUARED_ETX */

#if !RPL_MRHOF_SQUARED_ETX
/* Configuration parameters of RFC6719. Reject parents that have a higher
 * link metric than the following. The default value is 512 but we use 1024. */
#define MAX_LINK_METRIC     1024 /* Eq ETX of 8 */
/* Hysteresis of MRHOF: the rank must differ more than PARENT_SWITCH_THRESHOLD_DIV
 * in order to switch preferred parent. Default in RFC6719: 192, eq ETX of 1.5.
 * We use a more aggressive setting: 96, eq ETX of 0.75.
 */
#define PARENT_SWITCH_THRESHOLD 96 /* Eq ETX of 0.75 */
#else /* !RPL_MRHOF_SQUARED_ETX */
#define MAX_LINK_METRIC     2048 /* Eq ETX of 4 */
#define PARENT_SWITCH_THRESHOLD 160 /* Eq ETX of 1.25 (results in a churn comparable
to the threshold of 96 in the non-squared case) */
#endif /* !RPL_MRHOF_SQUARED_ETX */

/* Reject parents that have a higher path cost than the following. */
#define MAX_PATH_COST      32768   /* Eq path ETX of 256 */


double route_energy; // added by Riker

/*---------------------------------------------------------------------------*/
static void
reset(rpl_dag_t *dag)
{
  PRINTF("RPL: Reset MRHOF\n");
}
/*---------------------------------------------------------------------------*/
#if RPL_WITH_DAO_ACK
static void
dao_ack_callback(rpl_parent_t *p, int status)
{
  if(status == RPL_DAO_ACK_UNABLE_TO_ADD_ROUTE_AT_ROOT) {
    return;
  }
  /* here we need to handle failed DAO's and other stuff */
  PRINTF("RPL: MRHOF - DAO ACK received with status: %d\n", status);
  if(status >= RPL_DAO_ACK_UNABLE_TO_ACCEPT) {
    /* punish the ETX as if this was 10 packets lost */
    link_stats_packet_sent(rpl_get_parent_lladdr(p), MAC_TX_OK, 10);
  } else if(status == RPL_DAO_ACK_TIMEOUT) { /* timeout = no ack */
    /* punish the total lack of ACK with a similar punishment */
    link_stats_packet_sent(rpl_get_parent_lladdr(p), MAC_TX_OK, 10);
  }
}
#endif /* RPL_WITH_DAO_ACK */
/*---------------------------------------------------------------------------*/
static uint16_t
parent_link_metric(rpl_parent_t *p)
{
  const struct link_stats *stats = rpl_get_parent_link_stats(p);
  if(stats != NULL) {
#if RPL_MRHOF_SQUARED_ETX
    uint32_t squared_etx = ((uint32_t)stats->etx * stats->etx) / LINK_STATS_ETX_DIVISOR;
    return (uint16_t)MIN(squared_etx, 0xffff);
#else /* RPL_MRHOF_SQUARED_ETX */
  return stats->etx;
#endif /* RPL_MRHOF_SQUARED_ETX */
  }
  return 0xffff;
}



static uint16_t
parent_path_cost(rpl_parent_t *p)
{
  uint16_t base;

  if(p == NULL || p->dag == NULL || p->dag->instance == NULL) {
    return 0xffff;
  }

#if RPL_WITH_MC
  /* Handle the different MC types */
  switch(p->dag->instance->mc.type) {
    case RPL_DAG_MC_ETX:
      base = p->mc.obj.etx;
      break;
    case RPL_DAG_MC_ENERGY:
      base = p->mc.obj.energy.energy_est << 8;
      break;
    default:
      base = p->rank;
      break;
  }
#else /* RPL_WITH_MC */
  base = p->rank;
  //THIS IS THE RUNING CODE'S PART!!!
#endif /* RPL_WITH_MC */

  /* path cost upper bound: 0xffff */
  
  return MIN((uint32_t)base + parent_link_metric(p), 0xffff);
}

static rpl_rank_t
rank_via_parent(rpl_parent_t *p)
{
  uint16_t min_hoprankinc;
  uint16_t path_cost;

  if(p == NULL || p->dag == NULL || p->dag->instance == NULL) {
    return INFINITE_RANK;
  }

  min_hoprankinc = p->dag->instance->min_hoprankinc;
  path_cost = parent_path_cost(p);
  //printf("Corresponding Rank Value: %u\n", MAX(MIN((uint32_t)p->rank + min_hoprankinc, 0xffff), path_cost););
  //printf("Advertised Rank value: %u\n", p->rank);

  /* Rank lower-bound: parent rank + min_hoprankinc */
  
  return MAX(MIN((uint32_t)p->rank + min_hoprankinc, 0xffff), path_cost);
}
/*---------------------------------------------------------------------------*/
static int
parent_is_acceptable(rpl_parent_t *p)
{
  uint16_t link_metric = parent_link_metric(p);
  uint16_t path_cost = parent_path_cost(p);
  /* Exclude links with too high link metrics or path cost (RFC6719, 3.2.2) */
  //Riker code
  //parent is acceptable if it has a lower rank
  
  // begin of code changing
   rpl_dag_t *dag;
   dag = p->dag; // assuming both are in the same instance
   
  if(dag->preferred_parent =! NULL){
	return (link_metric <= MAX_LINK_METRIC) && (path_cost <= MAX_PATH_COST) && (p->rank < dag->rank);
  }
	return link_metric <= MAX_LINK_METRIC && path_cost <= MAX_PATH_COST;
	// end of code changing
}
/*---------------------------------------------------------------------------*/
static int
parent_has_usable_link(rpl_parent_t *p)
{
  uint16_t link_metric = parent_link_metric(p);
  /* Exclude links with too high link metrics  */
  return link_metric <= MAX_LINK_METRIC;
}



static rpl_parent_t *
best_parent(rpl_parent_t *p1, rpl_parent_t *p2)
{
  rpl_dag_t *dag;
  uint16_t p1_cost;
  uint16_t p2_cost;
  int p1_is_acceptable;
  int p2_is_acceptable;

  p1_is_acceptable = p1 != NULL && parent_is_acceptable(p1);
  p2_is_acceptable = p2 != NULL && parent_is_acceptable(p2);

  if(!p1_is_acceptable) {
    return p2_is_acceptable ? p2 : NULL;
  }
  if(!p2_is_acceptable) {
    return p1_is_acceptable ? p1 : NULL;
  }

  dag = p1->dag; // Both parents are in the same DAG. 
  //Alterar o custo para a máxima-menor energia
  p1_cost = p1->mc.obj.energy.energy_est;
  p2_cost = p2->mc.obj.energy.energy_est;
  
  // Maintain stability of the preferred parent in case of similar ranks.
  if(p1 == dag->preferred_parent || p2 == dag->preferred_parent) {
    if(p1_cost < p2_cost + PARENT_SWITCH_THRESHOLD &&
       p1_cost > p2_cost - PARENT_SWITCH_THRESHOLD) {
      return dag->preferred_parent;
    }
  }
  return p1_cost > p2_cost ? p1 : p2;
  
}


static rpl_dag_t *
best_dag(rpl_dag_t *d1, rpl_dag_t *d2)
{
  if(d1->grounded != d2->grounded) {
    return d1->grounded ? d1 : d2;
  }

  if(d1->preference != d2->preference) {
    return d1->preference > d2->preference ? d1 : d2;
  }

  return d1->rank < d2->rank ? d1 : d2;
}




/*---------------------------------------------------------------------------*/
// ----------- update_metric_container is used to uptate the metric -----------
//-----------------------------------------------------------------------------
#if !RPL_WITH_MC // IF RPL HAS NOT A METRIC CONTAINER SET
static void
update_metric_container(rpl_instance_t *instance)
{
  instance->mc.type = RPL_DAG_MC_NONE;
  PRINTF("RPL: Metric container set to NONE because RPL_WITH_MC=0 \n");
}
#else /* RPL_WITH_MC */
static void
update_metric_container(rpl_instance_t *instance)
{
  rpl_dag_t *dag;
  uint16_t path_cost;
  uint8_t type;

  dag = instance->current_dag;
  if(dag == NULL || !dag->joined) {
    PRINTF("RPL: Cannot update the metric container when not joined\n");
    return;
  }
  
  PRINTF("RPL: dag->rank is %d ROOT_RANK(instance) is %d \n", dag->rank, ROOT_RANK(instance));
  if(dag->rank == ROOT_RANK(instance)) {
    /* Configure MC at root only, other nodes are auto-configured when joining */
    PRINTF("RPL: Metric Container at root %u \n", RPL_DAG_MC);
    instance->mc.type = RPL_DAG_MC;
    instance->mc.flags = 0;
    instance->mc.aggr = RPL_DAG_MC_AGGR_ADDITIVE;
    instance->mc.prec = 0;
    path_cost = dag->rank;
  } else {
    path_cost = parent_path_cost(dag->preferred_parent);
  }

  /* Handle the different MC types */
  switch(instance->mc.type) {
    case RPL_DAG_MC_NONE:
    PRINTF("RPL updating MC to NONE \n");
      break;
      
      
    case RPL_DAG_MC_ETX:
      instance->mc.length = sizeof(instance->mc.obj.etx);
      instance->mc.obj.etx = path_cost;
      break;
      
      
   // ------- RIKER: CODE THAT RUNS WHEN ENERGY METRIC IS SET -------
    case RPL_DAG_MC_ENERGY:
      instance->mc.length = sizeof(instance->mc.obj.energy);
      if(dag->rank == ROOT_RANK(instance)) {
        type = RPL_DAG_MC_ENERGY_TYPE_MAINS; // CONSTANT POWER FOR ROOT
      } else {
        type = RPL_DAG_MC_ENERGY_TYPE_BATTERY; // BATTERY FOR NON-ROOTS
        
        
		#if PLATFORM_HAS_BATTERY_ESTIMATION
		instance->mc.obj.energy.flags = type << RPL_DAG_MC_ENERGY_TYPE;
		// Riker: Energy_est is Energy Estimation
		// Setting the energy estimation as the energy estimation of the node
		//instance->mc.obj.energy.energy_est = (unsigned) get_energy_estimation();
                instance->mc.obj.energy.energy_est = (unsigned) obj_func_Normalization(get_energy_estimation());
                instance->mc.obj.energy.has_harvesting = get_has_harvesting();
		#endif 

        }    
      break;
     // --------------------------------------------------
     
     
      
    default:
      PRINTF("RPL: MRHOF, non-supported MC %u\n", instance->mc.type);
      break;
  }
}
#endif /* RPL_WITH_MC */
/*---------------------------------------------------------------------------*/
rpl_of_t rpl_mrhof = {
  reset,
#if RPL_WITH_DAO_ACK
  dao_ack_callback,
#endif
  parent_link_metric,
  parent_has_usable_link,
  parent_path_cost,
  rank_via_parent,
  best_parent,
  best_dag,
  update_metric_container,
  RPL_OCP_MRHOF
};

#if PLATFORM_HAS_BATTERY_ESTIMATION
double get_energy_estimation(){
	// This is the energy estimation of its own battery
	// The battery charge should be normalized to a interval going from 0 to 100
	// 100 corresponds to the worse value - zero battery
	// 0 is corresponds to the maximal charge
	double energy_estimation, current_charge, max_charge;
	
	current_charge = get_battery_charge();
	
        //printf("1 energy - Current Charge: %lu\n", ((long unsigned) current_charge));
	
	max_charge = get_max_charge();
	
	//printf("2 energy - Max Available Constant: %lu\n", ((long unsigned)max_charge));
	
	energy_estimation = (current_charge / max_charge); // number between 0 - 100
	
	//printf("3 energy - Energy estimation (0 - 100): %lu\n", ((long unsigned) (energy_estimation*100)));
    
    return energy_estimation * 100;
}

	
double obj_func_Normalization(double energy_estimation){
	double norm_energy_estimation;
	
	norm_energy_estimation = energy_estimation * 1 ;
	return norm_energy_estimation;
	}
	


// This function is called when the aggregation will decide the agg level 
// It will return the max between the route_energy and it own energy_estimation
// Max because 1 means empty and 0 means full
double get_energy_on_route(){
	// It is necessary to undo obj_func_Normalization
	route_energy = energy_of_preferred_parent();
	//printf("5 energy - route energy is %u and my energy is %u \n",(unsigned) (route_energy*100), (unsigned) (get_energy_estimation()*100));
	return route_energy < get_energy_estimation() ? route_energy : get_energy_estimation();
}
#endif
