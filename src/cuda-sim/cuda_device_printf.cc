// Copyright (c) 2009-2011, Tor M. Aamodt
// The University of British Columbia
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
// Redistributions in binary form must reproduce the above copyright notice, this
// list of conditions and the following disclaimer in the documentation and/or
// other materials provided with the distribution.
// Neither the name of The University of British Columbia nor the names of its
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "cuda_device_printf.h"
#include "ptx_ir.h"

void decode_space( memory_space_t &space, ptx_thread_info *thread, const operand_info &op, memory_space *&mem, addr_t &addr);

void my_cuda_printf(const char *fmtstr,const char *arg_list)
{
   FILE *fp = stdout;
   unsigned i=0,j=0;
   unsigned arg_offset=0;
   char buf[64];
   bool in_fmt=false;
   while( fmtstr[i] ) {
      char c = fmtstr[i++];
      if( !in_fmt ) {
         if( c != '%' ) {
            fprintf(fp,"%c",c);
         } else {
            in_fmt=true;
            buf[0] = c;
            j=1;
         }
      } else {
         if(!( c == 'u' || c == 'f' || c == 'd' )) {
            printf("GPGPU-Sim PTX: ERROR ** printf parsing support is limited to %%u, %%f, %%d at present");
            abort();
         }
         buf[j] = c;
         buf[j+1] = 0;
         if( c == 'u' || c == 'd' ) {
            unsigned int *ptr = (unsigned int*)&arg_list[arg_offset]; 
            fprintf(fp,buf,*ptr);
            arg_offset += sizeof(unsigned int); 
         } else if( c == 'f' ) {
            // floats are converted to double for printf
            double *ptr = (double*)&arg_list[arg_offset]; 
            fprintf(fp,buf,*ptr);
            arg_offset += sizeof(double); 
         }
         in_fmt=false;
      }
   }
}

void gpgpusim_cuda_vprintf(const ptx_instruction * pI, ptx_thread_info * thread, const function_info * target_func ) 
{
      char *fmtstr = NULL;
      char *arg_list = NULL;
      unsigned n_return = target_func->has_return();
      unsigned n_args = target_func->num_args();
      bool tm_flag=false;
      if(thread->get_tm_manager()!=NULL)
    	  tm_flag=true;
      tm_access_uarch_info tm_access_info_dummy;
      mem_fetch mf_dummy;
      assert( n_args == 2 );
      for( unsigned arg=0; arg < n_args; arg ++ ) {
         const operand_info &actual_param_op = pI->operand_lookup(n_return+1+arg);
         const symbol *formal_param = target_func->get_arg(arg);
         unsigned size=formal_param->get_size_in_bytes();
         assert( formal_param->is_param_local() );
         assert( actual_param_op.is_param_local() );
         addr_t from_addr = actual_param_op.get_symbol()->get_address() + thread->get_local_mem_stack_pointer();
	 bool update_logical_info = false;

         unsigned long long buffer[1024];
         assert(size<1024*sizeof(unsigned long long));
         if(tm_flag)
            thread->get_tm_manager()->tm_access(thread->m_local_mem,local_space,true,from_addr,(void*)buffer,size,tm_access_info_dummy, &mf_dummy, update_logical_info);
         else
            thread->m_local_mem->read(from_addr,size,buffer);
         addr_t addr = (addr_t)buffer[0]; // should be pointer to generic memory location
         memory_space *mem=NULL;
         memory_space_t space = generic_space;
         decode_space(space,thread,actual_param_op,mem,addr); // figure out which space
         if( arg == 0 ) {
            unsigned len = 0;
            char b = 0;
            do { // figure out length
            	if(tm_flag)
            	   thread->get_tm_manager()->tm_access(mem,space,true,addr+len,&b,1,tm_access_info_dummy, &mf_dummy, update_logical_info);
            	else
            	   mem->read(addr+len,1,&b);
               len++;
            } while(b);
            fmtstr = (char*)calloc(len+64,sizeof(char));
            for( int i=0; i < len; i++ ) 
               {
             	 if(tm_flag)
		    thread->get_tm_manager()->tm_access(mem,space,true,addr+i,(fmtstr+i),1,tm_access_info_dummy, &mf_dummy, update_logical_info);
            	 else
         	    mem->read(addr+i,1,fmtstr+i);
               }
               //mem->read(addr,len,fmtstr);
         } else {
            unsigned len = thread->get_finfo()->local_mem_framesize();
            arg_list = (char*)calloc(len+64,sizeof(char));
            for( int i=0; i < len; i++ ) 
            	if(tm_flag)
                   thread->get_tm_manager()->tm_access(mem,space,true,addr+i,(arg_list+i),1,tm_access_info_dummy, &mf_dummy, update_logical_info);
            	else
            	   mem->read(addr+i,1,arg_list+i);
                //mem->read(addr,len,arg_list);
         }
      }
      my_cuda_printf(fmtstr,arg_list);
      free(fmtstr);
      free(arg_list);
}
