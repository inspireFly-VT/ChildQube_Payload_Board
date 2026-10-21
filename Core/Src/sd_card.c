/*
 * sd_card.c
 *
 *  Created on: Oct 21, 2026
 *      Author: mrith
 */

#include "sd_card.h"

/*
 * SD card SPI driver
 * STM32 HAL
 */

extern SPI_HandleTypeDef hspi1;

/*
 * IMPORTANT:
 * Replace these with the actual SD card
 * chip-select GPIO port and pin.
 *
 * Example:
 * #define SD_CS_PORT GPIOA
 * #define SD_CS_PIN  GPIO_PIN_4
 *
 * Do not assume PA4 is correct without
 * checking the board schematic.
 */
#ifndef SD_CS_PORT
#error "Define SD_CS_PORT using the board schematic"
#endif

#ifndef SD_CS_PIN
#error "Define SD_CS_PIN using the board schematic"
#endif

#define SD_TIMEOUT 1000U

#define CMD0   0
#define CMD8   8
#define CMD17  17
#define CMD24  24
#define CMD55  55
#define CMD58  58
#define ACMD41 41

static uint8_t sd_high_capacity = 0;

/* Select SD card */
static void SD_Select(void)
{
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN,
                      GPIO_PIN_RESET);
}

/* Deselect SD card */
static void SD_Deselect(void)
{
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN,
                      GPIO_PIN_SET);
}

/* Transfer one byte over SPI */
static uint8_t SD_SPI_Transfer(uint8_t data)
{
    uint8_t received = 0xFF;

    if (HAL_SPI_TransmitReceive(&hspi1, &data,
                                &received, 1,
                                SD_TIMEOUT) != HAL_OK)
    {
        return 0xFF;
    }

    return received;
}

/* Send additional clock pulses */
static void SD_SPI_Clock(void)
{
    SD_SPI_Transfer(0xFF);
}

/* Wait until SD card is ready */
static HAL_StatusTypeDef SD_WaitReady(void)
{
    uint32_t start = HAL_GetTick();

    do
    {
        if (SD_SPI_Transfer(0xFF) == 0xFF)
        {
            return HAL_OK;
        }
    }
    while ((HAL_GetTick() - start) < SD_TIMEOUT);

    return HAL_TIMEOUT;
}

/* Send SD card command */
static uint8_t SD_SendCommand(uint8_t cmd, uint32_t arg)
{
    uint8_t crc = 0x01;
    uint8_t response = 0xFF;

    if (SD_WaitReady() != HAL_OK)
    {
        return 0xFF;
    }

    if (cmd == CMD0)
    {
        crc = 0x95;
    }
    else if (cmd == CMD8)
    {
        crc = 0x87;
    }

    SD_SPI_Transfer(0x40 | cmd);

    SD_SPI_Transfer((uint8_t)(arg >> 24));
    SD_SPI_Transfer((uint8_t)(arg >> 16));
    SD_SPI_Transfer((uint8_t)(arg >> 8));
    SD_SPI_Transfer((uint8_t)arg);

    SD_SPI_Transfer(crc);

    for (int i = 0; i < 10; i++)
    {
        response = SD_SPI_Transfer(0xFF);

        if ((response & 0x80) == 0)
        {
            break;
        }
    }

    return response;
}

/* Initialize SD card */
HAL_StatusTypeDef SD_Init(void)
{
    uint8_t response;
    uint8_t r7[4];
    uint32_t start;

    sd_high_capacity = 0;

    SD_Deselect();

    /* Send at least 74 clock pulses */
    for (int i = 0; i < 10; i++)
    {
        SD_SPI_Clock();
    }

    SD_Select();

    response = SD_SendCommand(CMD0, 0);

    if (response != 0x01)
    {
        SD_Deselect();
        SD_SPI_Clock();
        return HAL_ERROR;
    }

    response = SD_SendCommand(CMD8, 0x1AA);

    if (response != 0x01)
    {
        SD_Deselect();
        SD_SPI_Clock();
        return HAL_ERROR;
    }

    for (int i = 0; i < 4; i++)
    {
        r7[i] = SD_SPI_Transfer(0xFF);
    }

    if (r7[2] != 0x01 || r7[3] != 0xAA)
    {
        SD_Deselect();
        SD_SPI_Clock();
        return HAL_ERROR;
    }

    start = HAL_GetTick();

    do
    {
        response = SD_SendCommand(CMD55, 0);

        if (response > 0x01)
        {
            break;
        }

        response = SD_SendCommand(ACMD41, 0x40000000);

        if (response == 0x00)
        {
            break;
        }
    }
    while ((HAL_GetTick() - start) < SD_TIMEOUT);

    if (response != 0x00)
    {
        SD_Deselect();
        SD_SPI_Clock();
        return HAL_TIMEOUT;
    }

    response = SD_SendCommand(CMD58, 0);

    if (response != 0x00)
    {
        SD_Deselect();
        SD_SPI_Clock();
        return HAL_ERROR;
    }

    uint8_t ocr[4];

    for (int i = 0; i < 4; i++)
    {
        ocr[i] = SD_SPI_Transfer(0xFF);
    }

    sd_high_capacity = (ocr[0] & 0x40) ? 1 : 0;

    SD_Deselect();
    SD_SPI_Clock();

    return HAL_OK;
}

/* Read one 512-byte block */
HAL_StatusTypeDef SD_ReadBlock(uint32_t block,
                               uint8_t *buffer)
{
    uint8_t response;
    uint8_t token = 0xFF;
    uint32_t start;

    if (buffer == NULL)
    {
        return HAL_ERROR;
    }

    if (!sd_high_capacity)
    {
        block *= SD_BLOCK_SIZE;
    }

    SD_Select();

    response = SD_SendCommand(CMD17, block);

    if (response != 0x00)
    {
        SD_Deselect();
        SD_SPI_Clock();
        return HAL_ERROR;
    }

    start = HAL_GetTick();

    do
    {
        token = SD_SPI_Transfer(0xFF);

        if (token == 0xFE)
        {
            break;
        }
    }
    while ((HAL_GetTick() - start) < SD_TIMEOUT);

    if (token != 0xFE)
    {
        SD_Deselect();
        SD_SPI_Clock();
        return HAL_TIMEOUT;
    }

    for (int i = 0; i < SD_BLOCK_SIZE; i++)
    {
        buffer[i] = SD_SPI_Transfer(0xFF);
    }

    /* Discard CRC */
    SD_SPI_Clock();
    SD_SPI_Clock();

    SD_Deselect();
    SD_SPI_Clock();

    return HAL_OK;
}

/* Write one 512-byte block */
HAL_StatusTypeDef SD_WriteBlock(uint32_t block,
                                const uint8_t *buffer)
{
    uint8_t response;

    if (buffer == NULL)
    {
        return HAL_ERROR;
    }

    if (!sd_high_capacity)
    {
        block *= SD_BLOCK_SIZE;
    }

    SD_Select();

    response = SD_SendCommand(CMD24, block);

    if (response != 0x00)
    {
        SD_Deselect();
        SD_SPI_Clock();
        return HAL_ERROR;
    }

    SD_SPI_Transfer(0xFF);
    SD_SPI_Transfer(0xFE);

    for (int i = 0; i < SD_BLOCK_SIZE; i++)
    {
        SD_SPI_Transfer(buffer[i]);
    }

    /* Dummy CRC */
    SD_SPI_Transfer(0xFF);
    SD_SPI_Transfer(0xFF);

    response = SD_SPI_Transfer(0xFF);

    if ((response & 0x1F) != 0x05)
    {
        SD_Deselect();
        SD_SPI_Clock();
        return HAL_ERROR;
    }

    if (SD_WaitReady() != HAL_OK)
    {
        SD_Deselect();
        SD_SPI_Clock();
        return HAL_TIMEOUT;
    }

    SD_Deselect();
    SD_SPI_Clock();

    return HAL_OK;
}


