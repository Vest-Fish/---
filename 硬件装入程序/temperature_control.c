#include <reg51.h>
#include <absacc.h>

// ������������
#define u8  unsigned char
#define u16 unsigned int
#define u32 unsigned long  

// ����˿�
#define DT_DA_PORT XBYTE[0xe400]     // ��������ݶ˿�
#define DT_DI_PORT XBYTE[0xe800]
#define PWM_OUT_PORT XBYTE[0xc400]   // ��������PWM����˿�
#define SPT_LOW_INPORT XBYTE[0xc100]
#define SPT_HIG_INPORT XBYTE[0xc200]

// �趨ֵ
int SetValue;

// PID���Ʋ���
float Kp = 0.5f;
float Ki = 0.017f;
float Kd = 0.30f;

// ������
int et = 0;
int et_1 = 0;
int et_2 = 0;

// PID���ֺ�΢����
float integral = 0.0f;
float derivative = 0.0f;

// PWM����
static float pwm = 0.0f;  

// ��ʾ������
u8 DispBuff[8] = {0, 0, 0, 0, 1, 1, 1, 7};

// �¶�����
u16 temperature = 0;

// ���ͱ�־
bit send_flag = 0;

// ��ʱ��0��ʼ��
void Timer0_Init(void) {
    TMOD |= 0x01;  // ���ö�ʱ��0Ϊģʽ1 (16λ��ʱ��)
    TH0 = 0x3C;    // ���ó�ֵ�Ա㶨ʱ 50ms (���辧��Ƶ��Ϊ11.0592MHz)
    TL0 = 0xB0;
    ET0 = 1;       // ʹ�ܶ�ʱ��0�ж�
    TR0 = 1;       // ������ʱ��0
}

// ���ڳ�ʼ��
void UART_Init(void) {
    SCON = 0x50;   // ���ô���Ϊģʽ1 (8λUART)
    TMOD |= 0x20;  // ���ö�ʱ��1Ϊģʽ2 (8λ�Զ���װ)
    TH1 = 0xFD;    // ������9600 (���辧��Ƶ��Ϊ11.0592MHz)
    TL1 = 0xFD;
        
    TR1 = 1;       // ������ʱ��1
    ES = 1;        // ʹ�ܴ����ж�
    EA = 1;        // ʹ��ȫ���ж�
}

// ����һ���ֽ�
void send_byte(u8 dat) {
    SBUF = dat;
    while (!TI);
    TI = 0;
}

// �����¶�����
void send_temperature(void) {
    send_byte(0x55);  // ��ʼ�ֽ�
    send_byte(0x02);  // �������� (�¶�)
    send_byte((u8)(temperature >> 8));  // ���ֽ�
    send_byte((u8)(temperature));       // ���ֽ�
    send_byte(0xaa);  // �����ֽ�
}

// ������ʾ
void update_display(void) {
    static u8 CurrentBit = 0;
    u8 SevenSegCode[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    u8 SevenSegBT[8] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

    DT_DI_PORT = 0;
    DT_DA_PORT = SevenSegCode[DispBuff[CurrentBit]];
    DT_DI_PORT = SevenSegBT[CurrentBit];

    CurrentBit++;
    if (CurrentBit >= 8) CurrentBit = 0;
}

// ��ȡ����������
u16 read_sensor(void) {
    u16 x;
    *((u8 *)&x + 1) = SPT_LOW_INPORT;
    *((u8 *)&x + 0) = SPT_HIG_INPORT;
    return x;
}

// �޷�����
float limit_value(float value, float min, float max) {
    if (value > max) return max;
    if (value < min) return min;
    return value;
}

// ������ʾ������
void update_display_buffer(u16 value) {
    // ������յ����ǷŴ�100����ֵ,��Ҫ�ȳ���100
    value = value / 100;
    DispBuff[4] = value / 1000;
    value %= 1000;
    DispBuff[5] = value / 100;
    value %= 100;
    DispBuff[6] = value / 10;
    DispBuff[7] = value % 10;
}

// PID���Ƽ���
void PID_Control(void) {
    float pid_output;
    u16 temp;  // ��ʱ���������ڱ����м���

    // ��ȡ����������
    temperature = read_sensor();

    // �������
    et_2 = et_1;
    et_1 = et;
    et = SetValue - (int)temperature;

    // ��������� (ʹ�����λ��ַ�)
    integral += et;
    integral = limit_value(integral, -1000, 1000);  // Ӧ�û����޷�

    // ����΢����
    derivative = et - et_1;

    // PID�����㷨
    pid_output = Kp * et + Ki * integral + Kd * derivative;

    // ����PWMֵ
    pwm += pid_output;
    pwm = limit_value(pwm, 0, 255);  // PWM�޷�

    // ���PWM
    PWM_OUT_PORT = (u8)pwm;

    // ʹ����ʱ����������ʾ������
    temp = temperature;
    update_display_buffer(temp);
}

// ������յ������ݰ�
void process_received_packet(u8 *buffer) {
    if (buffer[0] == 0x55 && buffer[1] == 0x01 && buffer[4] == 0xaa) {
        // �����¶��趨ֵ
        SetValue = (buffer[2] << 8) | buffer[3];
    }
}

// �����жϷ�����
void serial_isr(void) interrupt 4 {
    static u8 rx_buffer[5];  // ���ջ�����
    static u8 rx_index = 0;

    if (RI) {
        RI = 0;
        rx_buffer[rx_index++] = SBUF;

        // ����Ƿ���յ��������ݰ�
        if (rx_index == 5) {
            process_received_packet(rx_buffer);
            rx_index = 0;  // ��������
        }
    }
}

// ��ʱ��0�жϷ�����
void timer0_isr(void) interrupt 1 {
    // ��װ��ʱ����ֵ
    TH0 = 0x3C;
    TL0 = 0xB0;
    
    send_flag = 1;  // ���÷��ͱ�־
}

// ������
void main(void) {
    u8 send_counter = 0;
    UART_Init();    // ��ʼ������
    Timer0_Init();  // ��ʼ����ʱ��0
    while (1) {
        PID_Control();
        update_display();
        
        if (send_flag) {
            send_counter++;
            if (send_counter >= 5) {  // ÿ5�����ڷ���һ��
                send_temperature();
                send_counter = 0;
            }
            send_flag = 0;  // ���÷��ͱ�־
        }
        //send_temperature();
    }
}