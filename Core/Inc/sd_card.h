/*
 * sd card.h
 *
 *  Created on: Oct 21, 2026
 *      Author: mrithika & suhani
 */

#ifndef INC_SD_CARD_H_
#define INC_SD_CARD_H_

#include "main.h"
#include <stdint.h>

#define SD_BLOCK_SIZE 512
HAL_StatusTypeDef SD_Init(void);

HAL_StatusTypeDef SD_ReadBlock(uint32_t block,
                               uint8_t *buffer);

HAL_StatusTypeDef SD_WriteBlock(uint32_t block,
                                const uint8_t *buffer);



#endif /* INC_SD_CARD_H_ */
