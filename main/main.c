//esp idf framework includes
#include "sdkconfig.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_event.h"
#include "freertos/task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h" //interrupt queue
#include "driver/gpio.h" //gpio library for handling button and leds

//c library includes
#include <stdio.h>       // for printf
#include <sys/socket.h>  // for socket
#include <netdb.h>       // for gethostbyname
#include <unistd.h>      // for close
#include <string.h>      // for string manipulation
#include <errno.h>
#include <arpa/inet.h> // for converting IPv4 string into integer Internet Address
#include <time.h> //For tracking time elapsed between send and receive to and from server using clock()

#define R_LED_PIN 16
#define Y_LED_PIN 4
#define B_LED_PIN 0
#define G_LED_PIN 2

#define R_Button_PIN 33
#define Y_Button_PIN 25
#define B_Button_PIN 26
#define G_Button_PIN 27

#define MOTOR_PIN 32

static const char *TAG = "example"; //example tag
static const int ip_protocol = 0; //default value

//port we'll be connected to on machine with ip listed
static const char *ip = "192.168.17.240"; //ip of linux server
static const int port = 5566; //port number

//handler for interrupts
QueueHandle_t interruptQueue;

//task handlers 
TaskHandle_t Task0;
TaskHandle_t Task1;

//bool value for event handling
bool event_emergency = false;
bool event_safe = false;
bool event_drill = false;
bool event_shelter = false;

//Internal random access memory attribute, we use this memory space specifically for handling interrupts
static void IRAM_ATTR button_interrupt_handler(void *args)
{
    int pin = (int)args;
    xQueueSendFromISR(interruptQueue, &pin, NULL);
}

void LED_config(void)
{
    //set all LED pins to output
    esp_rom_gpio_pad_select_gpio(R_LED_PIN);
    gpio_set_direction(R_LED_PIN, GPIO_MODE_OUTPUT);

    esp_rom_gpio_pad_select_gpio(Y_LED_PIN);
    gpio_set_direction(Y_LED_PIN, GPIO_MODE_OUTPUT);

    esp_rom_gpio_pad_select_gpio(B_LED_PIN);
    gpio_set_direction(B_LED_PIN, GPIO_MODE_OUTPUT);

    esp_rom_gpio_pad_select_gpio(G_LED_PIN);
    gpio_set_direction(G_LED_PIN, GPIO_MODE_OUTPUT);
}

void Button_config(void)
{
    //set all button pins to input and pull up
    esp_rom_gpio_pad_select_gpio(G_Button_PIN);
    gpio_set_direction(G_Button_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(G_Button_PIN);
    gpio_pulldown_dis(G_Button_PIN);
    gpio_set_intr_type(G_Button_PIN, GPIO_INTR_NEGEDGE);

    esp_rom_gpio_pad_select_gpio(B_Button_PIN);
    gpio_set_direction(B_Button_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(B_Button_PIN);
    gpio_pulldown_dis(B_Button_PIN);
    gpio_set_intr_type(B_Button_PIN, GPIO_INTR_NEGEDGE);

    esp_rom_gpio_pad_select_gpio(Y_Button_PIN);
    gpio_set_direction(Y_Button_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(Y_Button_PIN);
    gpio_pulldown_dis(Y_Button_PIN);
    gpio_set_intr_type(Y_Button_PIN, GPIO_INTR_NEGEDGE);

    esp_rom_gpio_pad_select_gpio(R_Button_PIN);
    gpio_set_direction(R_Button_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(R_Button_PIN);
    gpio_pulldown_dis(R_Button_PIN);
    gpio_set_intr_type(R_Button_PIN, GPIO_INTR_NEGEDGE);
}

void setup_ISR(void){

    //allows us to add an individual isr handler for all pins
    gpio_install_isr_service(0);

    gpio_isr_handler_add(G_Button_PIN, button_interrupt_handler, (void *)G_Button_PIN);
    gpio_isr_handler_add(B_Button_PIN, button_interrupt_handler, (void *)B_Button_PIN);
    gpio_isr_handler_add(Y_Button_PIN, button_interrupt_handler, (void *)Y_Button_PIN);
    gpio_isr_handler_add(R_Button_PIN, button_interrupt_handler, (void *)R_Button_PIN);

}

void notify_server(char* event)
{
    //Create an IPv4 TPC socket file descriptor
    int sockfd = socket(AF_INET, SOCK_STREAM, ip_protocol);
    if (sockfd < 0){
        ESP_LOGE(TAG, "Unable to create socket: errorno %d", errno);
        return;
    }

    //set up sockaddr struct with server info
    struct sockaddr_in address;
    address.sin_family = AF_INET; //ipv4 
    address.sin_port = htons(port); //server port, big endian (network endianess is big endian)
    address.sin_addr.s_addr = inet_addr(ip); //translate string into integer ip address

    //Using struct socaddr instead of SA
    int err = connect(sockfd, (struct sockaddr*)&address, sizeof(address));
    if (err != 0){
        ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
        return;
    }

    //print statement
    ESP_LOGI(TAG, "Successfully connected");

    //send event message
    char buffer[1024];
    bzero(buffer, 1024);
    strcpy(buffer, event);
    ESP_LOGI(TAG, "Client: %s\n", buffer);
    send(sockfd, buffer, strlen(buffer), 0);

    //receive response from server
    //3 seconds
    bzero(buffer, 1024);
    recv(sockfd, buffer, sizeof(buffer), 0);
    ESP_LOGI(TAG, "Server: %s\n", buffer);
    
    //vTaskDelay(pdMS_to_TICKS(3000));

    if (sockfd != -1) {
        ESP_LOGI(TAG, "Notification sent");
        shutdown(sockfd, 0);
        close(sockfd);
    }

}
//Handles interrupts from buttons 
void USER_OUT_Task(void *params)
{
    int pin;
    //Infinite while loop to check for interrupts via task (CPU core allocated for task)
    while (1)
    {
        //using portMAX_DELAY as the amount of time we block waiting for interrupt
        if(xQueueReceive(interruptQueue, &pin, portMAX_DELAY))
        {
            char event[1024];
            bzero(event, 1024);
            if(pin == G_Button_PIN){
                //gpio_set_level(G_LED_PIN, 1);
                strcpy(event, "Safe");
                notify_server(event);
            }
            if(pin == Y_Button_PIN){
                //gpio_set_level(Y_LED_PIN, 1);
                strcpy(event, "Shelter");
                notify_server(event);
            }
            if(pin == B_Button_PIN){
                //gpio_set_level(B_LED_PIN, 1);
                strcpy(event, "Drill");
                notify_server(event);
            }
            if(pin == R_Button_PIN){
                //gpio_set_level(R_LED_PIN, 1);
                strcpy(event, "Emergency");
                notify_server(event);
            }
        }
    }
}

//turns on respective outputs 
void LED_MOTOR_Control(char* event){
    if (strcmp(event, "Safe") == 0){
        gpio_set_level(G_LED_PIN, 1);
        gpio_set_level(Y_LED_PIN, 0);
        gpio_set_level(B_LED_PIN, 0);
        gpio_set_level(R_LED_PIN, 0);

        event_emergency = false;
        event_safe = true;
        event_drill = false;
        event_shelter = false;
    }else if (strcmp(event, "Shelter") == 0){
        if(!event_shelter){
            gpio_set_level(G_LED_PIN, 0);
            //gpio_set_level(Y_LED_PIN, 1);
            gpio_set_level(B_LED_PIN, 0);
            gpio_set_level(R_LED_PIN, 0);
            for(int i = 0; i < 3; i++){
                gpio_set_level(Y_LED_PIN, 1);
                gpio_set_level(MOTOR_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(500));
                gpio_set_level(Y_LED_PIN, 0);
                gpio_set_level(MOTOR_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            gpio_set_level(Y_LED_PIN, 1);
            gpio_set_level(MOTOR_PIN, 0);
        }
        
        event_emergency = false;
        event_safe = false;
        event_drill = false;
        event_shelter = true;
    }else if (strcmp(event, "Drill") == 0){
        if(!event_drill){
            gpio_set_level(G_LED_PIN, 0);
            gpio_set_level(Y_LED_PIN, 0);
            //gpio_set_level(B_LED_PIN, 1);
            gpio_set_level(R_LED_PIN, 0);
            for(int i = 0; i < 3; i++){
                gpio_set_level(B_LED_PIN, 1);
                gpio_set_level(MOTOR_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(500));
                gpio_set_level(B_LED_PIN, 0);
                gpio_set_level(MOTOR_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            gpio_set_level(B_LED_PIN, 1);
            gpio_set_level(MOTOR_PIN, 0);
        }
        
        event_emergency = false;
        event_safe = false;
        event_drill = true;
        event_shelter = false;
    }else if (strcmp(event, "Emergency") == 0){
        if (!event_emergency){
            gpio_set_level(G_LED_PIN, 0);
            gpio_set_level(Y_LED_PIN, 0);
            gpio_set_level(B_LED_PIN, 0);
            //gpio_set_level(R_LED_PIN, 1);
            for(int i = 0; i < 3; i++){
                gpio_set_level(R_LED_PIN, 1);
                gpio_set_level(MOTOR_PIN, 1);
                vTaskDelay(pdMS_TO_TICKS(500));
                gpio_set_level(R_LED_PIN, 0);
                gpio_set_level(MOTOR_PIN, 0);
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            gpio_set_level(R_LED_PIN, 1);
            gpio_set_level(MOTOR_PIN, 0);
        }
        
        event_emergency = true;
        event_safe = false;
        event_drill = false;
        event_shelter = false;
    }
}

void tcp_client(void* params)
{
    while(1)
    {
        //Create an IPv4 TPC socket file descriptor
        int sockfd = socket(AF_INET, SOCK_STREAM, ip_protocol);
        if (sockfd < 0){
            ESP_LOGE(TAG, "Unable to create socket: errorno %d", errno);
            return;
        }

        //set up sockaddr struct with server info
        struct sockaddr_in address;
        address.sin_family = AF_INET; //ipv4 
        address.sin_port = htons(port); //server port, big endian (network endianess is big endian)
        address.sin_addr.s_addr = inet_addr(ip); //translate string into integer ip address

        //Using struct socaddr instead of SA
        int err = connect(sockfd, (struct sockaddr*)&address, sizeof(address));
        if (err != 0){
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            return;
        }

        //print statement
        ESP_LOGI(TAG, "Successfully connected");

        char buffer[1024];
        bzero(buffer, 1024);
        strcpy(buffer, "Event Inquiry");
        ESP_LOGI(TAG, "Client: %s\n", buffer);
        send(sockfd, buffer, strlen(buffer), 0);

        //receive response from server
        //3 seconds
        bzero(buffer, 1024);
        recv(sockfd, buffer, sizeof(buffer), 0);
        ESP_LOGI(TAG, "Server: %s\n", buffer);
        
        //vTaskDelay(pdMS_to_TICKS(3000));

        LED_MOTOR_Control(buffer);

        if (sockfd != -1) {
            ESP_LOGI(TAG, "Shutting down socket and restarting...");
            shutdown(sockfd, 0);
            close(sockfd);
        }

        
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    LED_config();
    Button_config();
    //Motor config 
    esp_rom_gpio_pad_select_gpio(MOTOR_PIN);
    gpio_set_direction(MOTOR_PIN, GPIO_MODE_OUTPUT);

    interruptQueue = xQueueCreate(10, sizeof(int));
    //2048 is memory allocated on CPU for this task
    //Assign it to core 0
    xTaskCreatePinnedToCore(tcp_client, "tcp_client", 100000, NULL, 1, &Task0, 0);
    //Assign it to core 1
    xTaskCreatePinnedToCore(USER_OUT_Task, "User_OUT_Task", 10000, NULL, 1, &Task1, 1);

    setup_ISR();
    //tcp_client();
}