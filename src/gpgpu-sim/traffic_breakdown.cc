#include "traffic_breakdown.h" 
#include "mem_fetch.h" 

void traffic_breakdown::print(FILE* fout)
{
   unsigned int tot_traffic = 0;
   for (traffic_stat_t::const_iterator i_stat = m_stats.begin(); i_stat != m_stats.end(); i_stat++) {
      unsigned int byte_transferred = 0; 
      for (traffic_class_t::const_iterator i_class = i_stat->second.begin(); i_class != i_stat->second.end(); i_class++) {
         byte_transferred += i_class->first * i_class->second;  // byte/packet x #packets
      }
      tot_traffic += byte_transferred;
      fprintf(fout, "traffic_breakdown_%s_%s = %u \n{", m_network_name.c_str(), i_stat->first.c_str(), byte_transferred);  
      for (traffic_class_t::const_iterator i_class = i_stat->second.begin(); i_class != i_stat->second.end(); i_class++) {
         fprintf(fout, "%u:%u,", i_class->first, i_class->second); 
      }
      fprintf(fout, "}\n"); 
   }
   fprintf(fout, "%s_total_traffic = %u\n", m_network_name.c_str(), tot_traffic);
}

void traffic_breakdown::record_traffic(class mem_fetch * mf, unsigned int size) 
{
   m_stats[classify_memfetch(mf)][size] += 1; 
}

std::string traffic_breakdown::classify_memfetch(class mem_fetch * mf)
{
   std::string traffic_name; 

   enum mem_access_type access_type = mf->get_access_type(); 

   switch (access_type) {
   case CONST_ACC_R:    
   case TEXTURE_ACC_R:   
   case GLOBAL_ACC_W:   
   case LOCAL_ACC_R:    
   case LOCAL_ACC_W:    
   case INST_ACC_R:     
   case L1_WRBK_ACC:    
   case L2_WRBK_ACC:    
   case L2_WR_ALLOC_R:  
      traffic_name = mem_access_type_str(access_type); 
      break; 
   case GLOBAL_ACC_R:   
      // check for global atomic operation 
      traffic_name = (mf->isatomic())? "GLOBAL_ATOMIC" : mem_access_type_str(GLOBAL_ACC_R); 
      break; 
   case TR_MSG:         traffic_name = "TR_MSG"; break; // legacy, should not appear in the stats 
   case TX_MSG: 
      traffic_name = "TX_MSG"; // KiloTM specific traffic 
      switch (mf->get_type()) {
      case TX_CU_ALLOC:         traffic_name += "_TX_CU_ALLOC"; break; 
      case TX_READ_SET:         traffic_name += "_TX_READ_SET"; break; 
      case TX_WRITE_SET:        traffic_name += "_TX_WRITE_SET"; break; 
      case TX_DONE_FILL:        traffic_name += "_TX_DONE_FILL"; break; 
      case TX_SKIP:             traffic_name += "_TX_SKIP"; break; 
      case TX_PASS:             traffic_name += "_TX_PASS"; break; 
      case TX_FAIL:             traffic_name += "_TX_FAIL"; break; 
      case CU_PASS:             traffic_name += "_CU_PASS"; break; 
      case CU_FAIL:             traffic_name += "_CU_FAIL"; break; 
      case CU_ALLOC_PASS:       traffic_name += "_CU_ALLOC_PASS"; break; 
      case CU_ALLOC_FAIL:       traffic_name += "_CU_ALLOC_FAIL"; break; 
      case CU_DONE_COMMIT:      traffic_name += "_CU_DONE_COMMIT"; break; 
      case NEWLY_INSERTED_ADDR: traffic_name += "_NEWLY_INSERTED_ADDR"; break; 
      case REMOVED_ADDR:        traffic_name += "_REMOVED_ADDR"; break; 
      default: assert(0 && "Access type and MemFetch type mismatch in traffic"); 
      }
      break; 
   default: assert(0 && "Unknown traffic type"); 
   }
   return traffic_name; 
}

