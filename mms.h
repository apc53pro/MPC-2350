#include "globals.h"

#ifndef MY_FUNCTIONS_H
#define MY_FUNCTIONS_H

int32_t alloc_partition(String name, partition_typ typ, uint32_t size_bytes);
int32_t delloc_partition(int index);

static inline __attribute__((always_inline))void write_in_partition(int index, uint32_t addr, int32_t val)//fazer funcao para escrever na particao, fazer funcao para ler na particao e criar "menu" grafico de vizualizacao de particoes da memoria
{
  
  if(partitions[index].start_addr + addr <= partitions[index].end_addr)
  {
    //memVarType[partitions[index].start_addr + addr] = 0;
    memory[partitions[index].start_addr + addr].value = val;

    //Serial.print("\nWROTE VALUE " + String(val) + " INTO PARTITION " + partitions[index].partition_name + " AT PARTITION ADDRESS " + String(addr) + " (MEMORY ADDRESS " + String(partitions[index].start_addr + addr) + ")");
    return;
  }else
  {
    
    while(1){Serial.print("\nTrying to write out of " +  partitions[index].partition_name + " partition bounds at address " + String(addr));}
    return;
  }
  
}

static inline __attribute__((always_inline))int32_t read_in_partition(int index, uint32_t addr)//fazer funcao para escrever na particao, fazer funcao para ler na particao e criar "menu" grafico de vizualizacao de particoes da memoria
{
  if(index < partition_amount && index > -1)
  {
    if(partitions[index].start_addr + addr <= partitions[index].end_addr)
    {
      //Serial.print("\nREAD VALUE " + String(memory[partitions[index].start_addr + addr].value.i) + " IN PARTITION " + partitions[index].partition_name + " AT PARTITION ADDRESS " + String(addr) + " (MEMORY ADDRESS " + String(partitions[index].start_addr + addr) + ")");
      return memory[partitions[index].start_addr + addr].value;
    }else
    {
      //Serial.print("\nTrying to read out of partition bounds...");
      return -2147483647;
    }
  }else
  {
    //Serial.print("\nERROR: INDEX OUT OF BOUNDS");
    return -2147483647;
  }
}

#endif
