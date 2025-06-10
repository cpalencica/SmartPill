# Code Readme

This readme should explain the contents of the code folder and subfolders
Make it easy for us to navigate this space.

## Reminders
- Describe here any software you have adopted from elsewhere by citation or URL
- Your code should include a header with your team member names and date

## Explanation

### Global variables 
We start off by initializing important variables to code operation such as variables for: 
- GPIOs
- thermistor and temperature calculations
- UART communication
- Data display variables

### ADC configuration: configure_ADC_photo(), configure_ADC_battery(), and configure_ADC_temp()
These three functions are in charge of properly configuring the ADC for the photocell, battery, and thermistor voltage dividers. These are later called in init().

### GPIO configuration: configure_GPIO()
This function is in charge of properly configuring the GPIOs for the buttons and LEDs. It also setups the interrupt request for the button GPIOs.

### Interrupt Request: button_isr_handler() and tilt_isr_handler()
These two interrupt request functions are both assigned to respond to button presses. When they are called they are each in charge of properly modifying a flag variable.

### Data Collection
- Light / Lux: The report_lux() function converts the voltage reading from the ADC channel to lux and stores it in a variable named lux. The conversion is done by using a linear equation that given a input analog voltage will convert it into a lux output. The linear equation is formed by calibrating the photocell using a lux meter.
- Battery Indicator: The report_battery() function reads the voltage from the ADC channel and stores it in a variable called final_voltage. This is done by readinig in the analog reading and converting it into an actual voltage. 
- Orientation: The report_tilt() function toggles the variable tilt between Vertical and Horizontal whenever the button is pressed.
- Timing: The time() function counts the number of seconds elasped and stores it in the variable time_elasped.

### Data Display
- The display_info() function uses the data collection variables to print Time, Temp, Light, Battery, and Tilt. It formats a string using sprintf and then writes that string over UART from the ESP32 to the laptop console

### initialization: init()
- init() is in charge of calling all the configuration functions in an organized manner

### main: app_main()
- app_main() calls init() and then makes tasks for all the data collection functions and for the data display function. This ensures that we can perform all the data collection and display in parallel.