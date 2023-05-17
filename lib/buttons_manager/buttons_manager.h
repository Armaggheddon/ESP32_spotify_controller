#ifndef BUTTONS_MANAGER_H
#define BUTTONS_MANAGER_H


#define BUTTONS_MANAGER_TAG "BUTTONS_MANAGER"
#define BUTTONS_MANAGER_DEFAULT_DEBOUNCE_TIME_US 300000 // = 300 ms 

typedef void (*ButtonCb)(void *button_info);

typedef struct{

    unsigned char mode;                 /** @brief Either GPIO_MODE_OUTPUT or GPIO_MODE_INPUT*/
    unsigned char pull_up_en;           /** @brief Either GPIO_PULLUP_ENABLE or GPIO_PULLUP_DISABLE */
    unsigned char pull_down_en;         /** @brief Either GPIO_PULLDOWN_ENABLE or GPIO_PULLDOWN_DISABLE */
    unsigned char intr_type;            /** @brief On which edge front to trigger the interrupt, e.g., GPIO_INTR_POSEDGE */
    
    unsigned short int id;              /** @brief A user defined ID */
    int button_pin;                     /** @brief The button's PIN number */
    int state;                          /** @brief The button's current state, maximum state is max_states - 1 */
    int max_states;                     /** @brief The button's maximum number of states */
    long int last_pressed;              /** @brief The button's last pressed time to perform debounce */

    ButtonCb cb;                       /** @brief The callback function to call when the button is pressed, is an user defined callback */

}ButtonInfo_t;

/**
 * Initialize the button array to hold the expected number of buttons
 * 
 * @param buttons_number    The maximum number of buttons that will be created
*/
void buttons_init(unsigned int buttons_number);


/**
 * Adds a button if does not already exist for the same pin and if 
 * the maximum number specified while calling buttons_init has not been 
 * already reached
 * 
 * @param mode              Input or output
 * 
 * @param pull_up_en        If to enable or disable the pullup resistance
 * 
 * @param pull_down_en      If to enable or disable the pulldown resistance
 * 
 * @param intr_type         The edge on which to trigger the interrupd
 * 
 * @param id                The user's button ID represented by a number, is
 *                          the user's responsibility to handle this ID
 * 
 * @param button_pin        The PIN number to assign the button to
 * 
 * @param state             The initial state of the button, must be less than
 *                          max_states, e.g., maximum state is max_states-1.
 *                          If state is greater than max_states - 1 will be set to 0
 * 
 * @param max_states        The maximum number of states that the button can have, e.g. for
 *                          a binary (classic) button use 2
 * 
 * @param last_pressed      The last time the button is pressed, this value is tipically initialized
 *                          to 0. However, setting this parameter to a number greater
 *                          than 0 will behave like a delay on when the button can be pressed
 * 
 * @param cb                The user's callback ISR when the buttons are pressed
*/
void buttons_add_button(
    unsigned char mode,
    unsigned char pull_up_en,
    unsigned char pull_down_en,
    unsigned char intr_type,
    unsigned short int id,
    int button_pin,
    int state,
    int max_states,
    long int last_pressed,
    ButtonCb cb);


/**
 * Starts and registers the isr handler for the buttons added in buttons_add_button. 
 * 
*/
void buttons_start();

#endif //BUTTONS_MANAGER_H