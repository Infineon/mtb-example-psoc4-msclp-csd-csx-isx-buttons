/******************************************************************************
 * File Name:   main.c
 *
 * Description: This is the source code for the PSoC 4: MSCLP low-power self-capacitance button
 *              Example for ModusToolbox.
 *
 * Related Document: See README.md
 *
 *
 *******************************************************************************
 * $ Copyright 2021-2023 Cypress Semiconductor $
 *******************************************************************************/

/*******************************************************************************
 * Include header files
 ******************************************************************************/
#include "cy_pdl.h"
#include "cybsp.h"
#include "cycfg.h"
#include "cycfg_capsense.h"

/*******************************************************************************
 * User configurable Macros
 ********************************************************************************/


/*******************************************************************************
 * Fixed Macros
 *******************************************************************************/
#define CAPSENSE_MSC0_INTR_PRIORITY     (3u)
#define CY_ASSERT_FAILED                (0u)
#define CAPSENSE_WIDGET_INACTIVE        (1u)
#define CYBSP_LED_ON                    (0u)
#define CYBSP_LED_OFF                   (1u)

/* EZI2C interrupt priority must be higher than CAPSENSE&trade; interrupt. */
#define EZI2C_INTR_PRIORITY             (2u)


/*******************************************************************************
 * Global Variables
 *******************************************************************************/
cy_stc_scb_ezi2c_context_t ezi2c_context;

/*******************************************************************************
 * Function Prototypes
 *******************************************************************************/
static void initialize_capsense(void);
static void capsense_msc0_isr(void);
static void ezi2c_isr(void);
static void initialize_capsense_tuner(void);
void led_control();


/*******************************************************************************
 * Function Name: main
 ********************************************************************************
 * Summary:
 *  System entrance point. This function performs
 *  - initial setup of device
 *  - initialize CAPSENSE&trade;
 *  - initialize tuner communication
 *  - scan touch input continuously
 *  - user LED for touch indication
 *
 * Return:
 *  int
 *
 *******************************************************************************/
int main(void)
{
    cy_rslt_t result;

    /* Initialize the device and board peripherals */
    result = cybsp_init();

    /* Board init failed. Stop program execution */
    if (result != CY_RSLT_SUCCESS)
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    /* Enable global interrupts */
    __enable_irq();

    /* Initialize EZI2C */
    initialize_capsense_tuner();

    /* Initialize MSC CAPSENSE */
    initialize_capsense();

    for (;;)
    {
        /*Scan all slots corresponding to the ISX buttons*/
        Cy_CapSense_ScanSlots(0u,4u,&cy_capsense_context);
        while(Cy_CapSense_IsBusy(&cy_capsense_context)){}

        /*Scan the slots corresponding to the CSD button*/
        Cy_CapSense_ScanSlots(4u,1u,&cy_capsense_context);
        while(Cy_CapSense_IsBusy(&cy_capsense_context)){}

        /*Scan the slots corresponding to the CSX button*/
        Cy_CapSense_ScanSlots(5u,1u,&cy_capsense_context);
        while(Cy_CapSense_IsBusy(&cy_capsense_context)){}

        /* Process all the widgets */
        Cy_CapSense_ProcessAllWidgets(&cy_capsense_context);

        /* Send capsense data to the Tuner */
        Cy_CapSense_RunTuner(&cy_capsense_context);

        led_control();
    }
}

/*******************************************************************************
 * Function Name: initialize_capsense
 ********************************************************************************
 * Summary:
 *  This function initializes the CAPSENSE&trade; and configures the CAPSENSE&trade;
 *  interrupt.
 *
 *******************************************************************************/
static void initialize_capsense(void)
{
    cy_capsense_status_t status = CY_CAPSENSE_STATUS_SUCCESS;

    /* CAPSENSE&trade; interrupt configuration MSCLP 0 */
    const cy_stc_sysint_t capsense_msc0_interrupt_config =
    {
            .intrSrc = CY_MSCLP0_LP_IRQ,
            .intrPriority = CAPSENSE_MSC0_INTR_PRIORITY,
    };

    /* Capture the MSC HW block and initialize it to the default state. */
    status = Cy_CapSense_Init(&cy_capsense_context);

    if (CY_CAPSENSE_STATUS_SUCCESS == status)
    {
        /* Initialize CAPSENSE&trade; interrupt for MSC 0 */
        Cy_SysInt_Init(&capsense_msc0_interrupt_config, capsense_msc0_isr);
        NVIC_ClearPendingIRQ(capsense_msc0_interrupt_config.intrSrc);
        NVIC_EnableIRQ(capsense_msc0_interrupt_config.intrSrc);

        status = Cy_CapSense_Enable(&cy_capsense_context);
    }

    if(status != CY_CAPSENSE_STATUS_SUCCESS)
    {
        /* This status could fail before tuning the sensors correctly.
         * Ensure that this function passes after the CAPSENSE&trade; sensors are tuned
         * as per procedure give in the Readme.md file */
    }
}

/*******************************************************************************
 * Function Name: capsense_msc0_isr
 ********************************************************************************
 * Summary:
 *  Wrapper function for handling interrupts from CAPSENSE&trade; MSC0 block.
 *
 *******************************************************************************/
static void capsense_msc0_isr(void)
{
    Cy_CapSense_InterruptHandler(CY_MSCLP0_HW, &cy_capsense_context);
}

/*******************************************************************************
 * Function Name: initialize_capsense_tuner
 ********************************************************************************
 * Summary:
 *  EZI2C module to communicate with the CAPSENSE&trade; Tuner tool.
 *
 *******************************************************************************/
static void initialize_capsense_tuner(void)
{
    cy_en_scb_ezi2c_status_t status = CY_SCB_EZI2C_SUCCESS;

    /* EZI2C interrupt configuration structure */
    const cy_stc_sysint_t ezi2c_intr_config =
    {
            .intrSrc = CYBSP_EZI2C_IRQ,
            .intrPriority = EZI2C_INTR_PRIORITY,
    };

    /* Initialize the EzI2C firmware module */
    status = Cy_SCB_EZI2C_Init(CYBSP_EZI2C_HW, &CYBSP_EZI2C_config, &ezi2c_context);

    if(status != CY_SCB_EZI2C_SUCCESS)
    {
        CY_ASSERT(CY_ASSERT_FAILED);
    }

    Cy_SysInt_Init(&ezi2c_intr_config, ezi2c_isr);
    NVIC_EnableIRQ(ezi2c_intr_config.intrSrc);

    /* Set the CAPSENSE&trade; data structure as the I2C buffer to be exposed to the
     * master on primary slave address interface. Any I2C host tools such as
     * the Tuner or the Bridge Control Panel can read this buffer but you can
     * connect only one tool at a time.
     */
    Cy_SCB_EZI2C_SetBuffer1(CYBSP_EZI2C_HW, (uint8_t *)&cy_capsense_tuner,
            sizeof(cy_capsense_tuner), sizeof(cy_capsense_tuner),
            &ezi2c_context);

    Cy_SCB_EZI2C_Enable(CYBSP_EZI2C_HW);

}

/*******************************************************************************
 * Function Name: ezi2c_isr
 ********************************************************************************
 * Summary:
 *  Wrapper function for handling interrupts from EZI2C block.
 *
 *******************************************************************************/
static void ezi2c_isr(void)
{
    Cy_SCB_EZI2C_Interrupt(CYBSP_EZI2C_HW, &ezi2c_context);
}

/*******************************************************************************
 * Function Name: led_control
 ********************************************************************************
 * Summary:
 *  Control LEDs in the kit to show touch activation of different sensors
 *******************************************************************************/
void led_control()
{

    if (CAPSENSE_WIDGET_INACTIVE != Cy_CapSense_IsWidgetActive(CY_CAPSENSE_CSD_BUTTON_WDGT_ID, &cy_capsense_context))
    {
        Cy_GPIO_Write(CYBSP_USER_LED2_PORT, CYBSP_USER_LED2_NUM, CYBSP_LED_ON);
    }
    else
    {
        Cy_GPIO_Write(CYBSP_USER_LED2_PORT, CYBSP_USER_LED2_NUM, CYBSP_LED_OFF);
    }


    if (CAPSENSE_WIDGET_INACTIVE != Cy_CapSense_IsWidgetActive(CY_CAPSENSE_CSX_BUTTON_WDGT_ID, &cy_capsense_context))
    {
        Cy_GPIO_Write(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_NUM, CYBSP_LED_ON);
    }
    else
    {
        Cy_GPIO_Write(CYBSP_USER_LED1_PORT, CYBSP_USER_LED1_NUM, CYBSP_LED_OFF);
    }


    if (CAPSENSE_WIDGET_INACTIVE != Cy_CapSense_IsWidgetActive(CY_CAPSENSE_ISX_BUTTON1_WDGT_ID, &cy_capsense_context))
    {
        Cy_GPIO_Write(CYBSP_ISX_LED1_PORT, CYBSP_ISX_LED1_NUM, CYBSP_LED_ON);
    }
    else
    {
        Cy_GPIO_Write(CYBSP_ISX_LED1_PORT, CYBSP_ISX_LED1_NUM, CYBSP_LED_OFF);
    }

    if (CAPSENSE_WIDGET_INACTIVE != Cy_CapSense_IsWidgetActive(CY_CAPSENSE_ISX_BUTTON2_WDGT_ID, &cy_capsense_context))
    {
        Cy_GPIO_Write(CYBSP_ISX_LED2_PORT, CYBSP_ISX_LED2_NUM, CYBSP_LED_ON);
    }
    else
    {
        Cy_GPIO_Write(CYBSP_ISX_LED2_PORT, CYBSP_ISX_LED2_NUM, CYBSP_LED_OFF);
    }

    if (CAPSENSE_WIDGET_INACTIVE != Cy_CapSense_IsWidgetActive(CY_CAPSENSE_ISX_BUTTON3_WDGT_ID, &cy_capsense_context))
    {
        Cy_GPIO_Write(CYBSP_ISX_LED3_PORT, CYBSP_ISX_LED3_NUM, CYBSP_LED_ON);
    }
    else
    {
        Cy_GPIO_Write(CYBSP_ISX_LED3_PORT, CYBSP_ISX_LED3_NUM, CYBSP_LED_OFF);
    }

    if (CAPSENSE_WIDGET_INACTIVE != Cy_CapSense_IsWidgetActive(CY_CAPSENSE_ISX_BUTTON4_WDGT_ID, &cy_capsense_context))
    {
        Cy_GPIO_Write(CYBSP_ISX_LED4_PORT, CYBSP_ISX_LED4_NUM, CYBSP_LED_ON);
    }
    else
    {
        Cy_GPIO_Write(CYBSP_ISX_LED4_PORT, CYBSP_ISX_LED4_NUM, CYBSP_LED_OFF);
    }

}

/* [] END OF FILE */
