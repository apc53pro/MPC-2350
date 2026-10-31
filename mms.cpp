#include "mms.h"
#include "globals.h"

uint8_t vga_data_array[TXCOUNT];
uint8_t * address_pointer = &vga_data_array[0] ;

// This is a CPU-rendered staging buffer, not the buffer scanned by VGA DMA.
// Keeping it in PSRAM leaves enough internal SRAM heap for I2S.begin().
PSRAM uint8_t vga_640_480_4_01[VGA_4BIT_BUFFER_SIZE];

PSRAM var memory[memory_size];
partition partitions[partition_amount];

int32_t alloc_partition(String name, partition_typ typ, uint32_t size_bytes)
{
  uint32_t free_space_check_start = 0;

  for(int j = 0; j < partition_amount; j++)
  {
    for(int i = 0; i < partition_amount; i++)
    {
      if(partitions[i].partition_type != VACANT)
      {
        if((free_space_check_start <= partitions[i].end_addr) && (partitions[i].start_addr <= free_space_check_start+(size_bytes-1)))
        {
          //Serial.print("\nINTERSECTION");
          free_space_check_start = partitions[i].end_addr + 1;

          break;
        }
      }

      if(i == partition_amount-1)
      {
        for(int k = 0; k < partition_amount; k++)
        {
          if(partitions[k].partition_type == VACANT)
          {
            if(name.length() > partition_name_max)
            {
              Serial.print("\nPARTITION ALLOCATION ERROR: NAME TOO LARGE");
              return -1;
            }

            if(free_space_check_start+(size_bytes-1) >= memory_size)
            {
              Serial.print("\nERROR: OUT OF BOUNDS");
              break;
            }

            name.trim();
            partitions[k].partition_name = name;
            partitions[k].start_addr = free_space_check_start;
            partitions[k].end_addr = free_space_check_start+(size_bytes-1);
            partitions[k].partition_type = typ;

            Serial.print("\nALLOCATED NEW SPACE FROM &");
            Serial.print(free_space_check_start);

            Serial.print(" TO &");
            Serial.print(free_space_check_start+(size_bytes-1));

            Serial.print(" WITH INDEX ");
            Serial.print(k);

            Serial.print(" AND NAME " + partitions[k].partition_name);

            return k;

          }
        }
      }
    }
  }

  Serial.print("\nCOULD NOT ALLOCATE");
  return -1;

}

int32_t delloc_partition(int index)
{
  if(index < partition_amount && index > -1)
  {
    if(partitions[index].partition_type != SYSTEM)
    {
      partitions[index].partition_type = VACANT;
      Serial.print("\nDEALLOCATED PARTITION \"" + partitions[index].partition_name + "\" OF INDEX " + String(index) + " SUCCESSFULLY.");
      return 1;
    }else
    {
      
      Serial.print("\nERROR: CANNOT DEALLOCATE SYSTEM RESERVED PARTITIONS");

      return -1;
    }
  }else
  {
    //Serial.print("\nERROR: INDEX OUT OF BOUNDS");
    return -1;
  }
  return -1;
}

int32_t defrag_memory() //This function will go through all partitions from left to right, and if they can be shifted left at all without intersecting with another partition, it will be, as much as possible. Thus defragmenting memory.
{
  return 1;
}

void write_in_partition(String name, uint32_t addr, float val)//fazer funcao para escrever na particao, fazer funcao para ler na particao e criar "menu" grafico de vizualizacao de particoes da memoria
{

}

