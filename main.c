#include "xparameters.h"
#include "xgpio.h" // access to functions like XGpio_Initialize() XGpio_DiscreteRead() XGpio_DiscreteWrite()
#include "xil_printf.h"
#include "sleep.h"
#include <stdlib.h> // For abs()

// Define Device IDs from xparameters.h (these match standard Vivado IP naming)
// Use _BASEADDR instead of _DEVICE_ID for Vitis Unified IDE
#define switches_id   XPAR_AXI_GPIO_SWITCHES_BASEADDR
#define buttons_id    XPAR_AXI_GPIO_BUTTONS_BASEADDR
#define leds_id       XPAR_AXI_GPIO_LED_BASEADDR
#define seven_seg_id    XPAR_AXI_GPIO_7_SEGMENT_BASEADDR

// AXI GPIO Channel Definitions
#define channel_1 1 //anodes
#define channel_2 2 //segment lines

// 7-Segment display in Basys3 is Active Low, so 0xC0  actually turns ON the display
// Format: DP, G, F, E, D, C, B, A
const u8 seg_digits[10] = {
    0xC0, // 0
    0xF9, // 1
    0xA4, // 2
    0xB0, // 3
    0x99, // 4
    0x92, // 5
    0x82, // 6
    0xF8, // 7
    0x80, // 8
    0x90  // 9
};

const u8 seg_minus = 0xBF; // Minus sign (-) (when used in subtraction operation)
const u8 seg_blank = 0xC0; // All segments zero, just to showcase that its working, if not use OxFF for all segments OFF

// Global GPIO Instances
XGpio gpio_sw, gpio_btn, gpio_led, gpio_seg; //software objects representing hardware peripherals.

// Function Prototypes
void display_7seg(int number);

int main() {
    int status; 
    u16 sw_data;
    u8 btn_data;
    int operand_a, operand_b;
    int result = 0;

    xil_printf("starting!\r\n");

    // GPIO initialization - connects software object to hardware peripheral.
    //uses bitwise OR assignment, if any initialization fails
    status = XGpio_Initialize(&gpio_sw, switches_id);
    status |= XGpio_Initialize(&gpio_btn, buttons_id);
    status |= XGpio_Initialize(&gpio_led, leds_id);
    status |= XGpio_Initialize(&gpio_seg, seven_seg_id);

    if (status != XST_SUCCESS) {
        xil_printf("GPIO initialization failed!\r\n"); //Error Check
        return XST_FAILURE; //end program
    }

    // set GPIO data directions (1 = Input, 0 = Output)
    XGpio_SetDataDirection(&gpio_sw, channel_1, 0xFFFF); // 16 Switches IN
    XGpio_SetDataDirection(&gpio_btn, channel_1, 0x0F);  // 4 Buttons IN
    XGpio_SetDataDirection(&gpio_led, channel_1, 0x0000); // 16 LEDs OUT
    
    // based on block design, the 7-segment IP has two channels
    XGpio_SetDataDirection(&gpio_seg, channel_1, 0x00);   // Anodes OUT
    XGpio_SetDataDirection(&gpio_seg, channel_2, 0x00);   // Segments OUT

    // main infinite loop
    while (1) {
        // read inputs
        sw_data = XGpio_DiscreteRead(&gpio_sw, channel_1);
        btn_data = XGpio_DiscreteRead(&gpio_btn, channel_1);

        
        XGpio_DiscreteWrite(&gpio_led, channel_1, sw_data);// copies switch values to LEDs.

        // extract 4-bit Operands
        operand_a = sw_data & 0x000F;         // switches 0-3
        operand_b = (sw_data >> 4) & 0x000F;  // switches 4-7

        // Determine Operation based on Button Press
        if (btn_data & 0x01) {        // Button 0 Up (BTNU)
            result = operand_a + operand_b;
        } else if (btn_data & 0x02) { // Button 1 Left (BTNL)
            result = operand_a - operand_b;    
        } else if (btn_data & 0x04) { // Button 2 Right (BTNR)
            result = operand_a * operand_b;
        } else if (btn_data & 0x08) { // Button 3 Down (BTND)
            if (operand_b != 0) {
                result = operand_a / operand_b;
            } else {
                result = 9999; // Error code for Divide by Zero
            }
        }

        // Multiplex the display (this needs to run continuously to trick the eye)
        // We run a quick loop of 50 cycles before polling inputs again to keep the display bright
        for (int i = 0; i < 50; i++) {
            display_7seg(result);
        }
    }

    return 0;
}

// Function to handle multiplexing of the 7-segment display
void display_7seg(int number) {
    u8 digits[4] = {seg_blank, seg_blank, seg_blank, seg_blank};
    int is_negative = 0;
    
    // Check for error code
    if (number == 9999) {
        digits[0] = 0x86; // 'E'
        digits[1] = 0xAF; // 'r'
        digits[2] = 0xAF; // 'r'
        digits[3] = seg_blank;
    } else {
        // Handle negative numbers
        if (number < 0) {
            is_negative = 1;
            number = abs(number);
        }

        // Extract decimal digits
        digits[0] = seg_digits[number % 10];             // Ones
        if (number > 9 || is_negative)  digits[1] = seg_digits[(number / 10) % 10]; // Tens
        if (number > 99 || is_negative) digits[2] = seg_digits[(number / 100) % 10]; // Hundreds
        
        // Apply negative sign to the highest unused digit
        if (is_negative) {
            if (number < 10) digits[1] = seg_minus;
            else if (number < 100) digits[2] = seg_minus;
            else digits[3] = seg_minus;
        }
    }

    // multiplexing Loop: Turn on one anode at a time
    for (int i = 0; i < 4; i++) {
        // turn OFF all anodes first to prevent ghosting
        XGpio_DiscreteWrite(&gpio_seg, channel_1, 0x0F); 
        
        // write the segment pattern for the current digit
        XGpio_DiscreteWrite(&gpio_seg, channel_2, digits[i]);
        
        // turn ON the specific anode (Active Low, so 0 turns it ON)
        u8 anode_ctrl = ~(1 << i) & 0x0F; 
        XGpio_DiscreteWrite(&gpio_seg, channel_1, anode_ctrl);
        
        //small delay to allow the LED to illuminate (adjust between 1000-5000 for flicker control)
        usleep(2500); 
    }
}