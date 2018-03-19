#include "usb_cdc.h"
#include "usb_endp.h"
#include <string.h>
#include "i2c.h"
#include "bootloader.h"

#include "ch554_platform.h"

//��ʼ��������Ϊ57600��1ֹͣλ����У�飬8����λ��
xdatabuf(LINECODING_ADDR, LineCoding, LINECODING_SIZE);

#define SI5351_ReferenceClock	"26000000"
#define Device_Version			"1.0.1"

extern uint8_t UsbConfig;
idata uint8_t  Receive_Uart_Buf[UART_REV_LEN];
idata volatile uint8_t Uart_Input_Point = 0;   //ѭ��������д��ָ�룬���߸�λ��Ҫ��ʼ��Ϊ0
idata volatile uint8_t Uart_Output_Point = 0;  //ѭ��������ȡ��ָ�룬���߸�λ��Ҫ��ʼ��Ϊ0
idata volatile uint8_t UartByteCount = 0;	  //��ǰ������ʣ���ȡ�ֽ���
idata volatile uint8_t USBByteCount = 0;	  //����USB�˵���յ�������
idata volatile uint8_t USBBufOutPoint = 0;	//ȡ����ָ��
idata volatile uint8_t UpPoint2_Busy  = 0;   //�ϴ��˵��Ƿ�æ��־
uint32_t CDC_Baud = 0;


void USB_EP1_IN(void) {
	UEP1_T_LEN = 0;
	UEP1_CTRL = UEP1_CTRL & ~ MASK_UEP_T_RES | UEP_T_RES_NAK;
}

void USB_EP2_IN(void) {
	UEP2_T_LEN = 0;
	UEP2_CTRL = UEP2_CTRL & ~ MASK_UEP_T_RES | UEP_T_RES_NAK;
	UpPoint2_Busy = 0;
}

void USB_EP2_OUT(void) {
	if (!U_TOG_OK )
		return;

	USBByteCount = USB_RX_LEN;
	USBBufOutPoint = 0;											 //ȡ����ָ�븴λ
	UEP2_CTRL = UEP2_CTRL & ~ MASK_UEP_R_RES | UEP_R_RES_NAK;	   //�յ�һ�����ݾ�NAK�������������꣬���������޸���Ӧ��ʽ
}

void CDC_InitBaud(void) {
	LineCoding[0] = 0x00;
	LineCoding[1] = 0xE1;
	LineCoding[2] = 0x00;
	LineCoding[3] = 0x00;
	LineCoding[4] = 0x00;
	LineCoding[5] = 0x00;
	LineCoding[6] = 0x08;
}

void CDC_SetBaud(void) {
	U32_XLittle(&CDC_Baud, LineCoding);

	//*((uint8_t *)&CDC_Baud) = LineCoding[0];
	//*((uint8_t *)&CDC_Baud+1) = LineCoding[1];
	//*((uint8_t *)&CDC_Baud+2) = LineCoding[2];
	//*((uint8_t *)&CDC_Baud+3) = LineCoding[3];

	if(CDC_Baud > 999999)
		CDC_Baud = 57600;
}

void virtual_uart_tx(uint8_t tdata) {
	Receive_Uart_Buf[Uart_Input_Point++] = tdata;
	UartByteCount++;					//��ǰ������ʣ���ȡ�ֽ���
	if(Uart_Input_Point>=UART_REV_LEN)
	{
		Uart_Input_Point = 0;
	}
}

void v_uart_puts(char *str)
{
	while(*str)
		virtual_uart_tx(*(str++));
}

void usb_poll()
{
	uint8_t length;
	static uint8_t Uart_Timeout = 0;
	if(UsbConfig)
	{
		if(UartByteCount)
			Uart_Timeout++;
		if(!UpPoint2_Busy)   //�˵㲻��æ�����к�ĵ�һ�����ݣ�ֻ���������ϴ���
		{
			length = UartByteCount;
			if(length>0)
			{
				if(length>39 || Uart_Timeout>100)
				{
					Uart_Timeout = 0;
					if(Uart_Output_Point+length>UART_REV_LEN)
						length = UART_REV_LEN-Uart_Output_Point;
					UartByteCount -= length;
					//д�ϴ��˵�
					memcpy(Ep2Buffer+MAX_PACKET_SIZE,&Receive_Uart_Buf[Uart_Output_Point],length);
					Uart_Output_Point+=length;
					if(Uart_Output_Point>=UART_REV_LEN)
						Uart_Output_Point = 0;
					UEP2_T_LEN = length;													//Ԥʹ�÷��ͳ���һ��Ҫ���
					UEP2_CTRL = UEP2_CTRL & ~ MASK_UEP_T_RES | UEP_T_RES_ACK;			//Ӧ��ACK
					UpPoint2_Busy = 1;
				}
			}
		}
	}
}


void uart_poll()
{/* ���� ������� */
	uint8_t uart_data;
	uint8_t i2c_ack = 0;
//	static bool i2c_status = 0;
	static uint8_t i2c_frame_len = 0;
	static uint8_t i2c_frame_rx_len = 0;
	static uint8_t i2c_error_no = 0;
	static uint8_t uart_rx_status = 0;
	static uint8_t former_data = 0;
	static uint8_t dontstop = 0;
	uint8_t i;

	if(USBByteCount)   //USB���ն˵�������
	{
		uart_data = Ep2Buffer[USBBufOutPoint++];

		if(uart_rx_status == 0)
		{
			if(uart_data == 'Q')
			{
				v_uart_puts(SI5351_ReferenceClock); /* 26MHz Crystal */
				v_uart_puts("\r\n");
			}
			else if(uart_data == 'V')
			{ /* Version */
				v_uart_puts(Device_Version); /* Device version */
				v_uart_puts("\r\n");
			}
			else if(uart_data == 'E')
			{
				JumpToBootloader();

			}
			else if(uart_data == 'B')
			{
				virtual_uart_tx(CDC_Baud / 100000 + '0');
				virtual_uart_tx(CDC_Baud % 100000 / 10000 + '0');
				virtual_uart_tx(CDC_Baud % 10000 / 1000 + '0');
				virtual_uart_tx(CDC_Baud % 1000 / 100 + '0');
				virtual_uart_tx(CDC_Baud % 100 / 10 + '0');
				virtual_uart_tx(CDC_Baud % 10 / 1 + '0');
				v_uart_puts("\r\n");
			}
			else if(uart_data == 'T' && former_data != 'A') /* BAN AT commands */
			{ /* Transmit I2C Data: T: <LEN>, 16bytes data, performing S, <AR>, <DAT>, E */

				i2c_frame_rx_len = 0;
				uart_rx_status = 1;
				i2c_error_no = 0;
				i2c_frame_len = 0;
			}
			else if(uart_data == 'R')
			{ /* Recieve I2C Data: R<AR><LEN> */
				i2c_frame_rx_len = 0;
				uart_rx_status = 3;
				i2c_error_no = 0;
				i2c_frame_len = 0;
			}
			else if(uart_data == 'T' && former_data == 'A') /* BAN AT commands */
			{
				v_uart_puts("OK\r\n");
			}
			else if(uart_data == 'A')
			{
			}
			else
			{
				v_uart_puts("NOT SUPPORTED\r\n");
			}
		}
		else if(uart_rx_status == 1)
		{ // 54	03	C0	02	53
			i2c_frame_len = uart_data & 0x3f; /* �õ����� */
			if(uart_data & 0x80)
				dontstop = 1;
			else
				dontstop = 0;

			I2C_Send_Start();
			uart_rx_status = 2;
		}
		else if(uart_rx_status == 2)
		{
			if(i2c_error_no == 0)
			{
				I2C_Buf = uart_data;
				I2C_WriteByte();
				i2c_ack = I2C_Buf;
				if(i2c_ack != 1)
				{
					I2C_Send_Stop();
					i2c_error_no = i2c_frame_rx_len + 1;
				}
			}
			i2c_frame_rx_len ++;

			if(i2c_frame_len == i2c_frame_rx_len)
			{
				if(i2c_error_no == 0)
				{
					v_uart_puts("OK\r\n");
					if(dontstop == 0)
						I2C_Send_Stop(); /* ֹͣI2C */
				}
				else
				{
					virtual_uart_tx('F');
					virtual_uart_tx(i2c_error_no / 10 + '0');
					virtual_uart_tx(i2c_error_no % 10 + '0'); /* ����ʧ�� */
					v_uart_puts("\r\n");
				}

				i2c_frame_len = 0;
				i2c_frame_rx_len = 0;
				uart_rx_status = 0;
				i2c_error_no = 0;
			}

		}
		else if(uart_rx_status == 3)
		{
			I2C_Send_Start();
			I2C_Buf = uart_data;
			I2C_WriteByte();
			i2c_ack = I2C_Buf;
			if(i2c_ack != 1)
			{
				v_uart_puts("FAIL\r\n");
				I2C_Send_Stop();
				uart_rx_status = 0;
			}
			uart_rx_status = 4;
		}
		else if(uart_rx_status == 4)
		{
			i2c_frame_len = uart_data & 0x3f;
			for(i = 0; i < i2c_frame_len; i++)
			{
				I2C_ReadByte();
				virtual_uart_tx(I2C_Buf);
				//if(i2c_ack != 1)
					//break;
			}
			I2C_Send_Stop();
			uart_rx_status = 0;
		}
		former_data = uart_data;

		USBByteCount--;

		if(USBByteCount==0)
			UEP2_CTRL = UEP2_CTRL & ~ MASK_UEP_R_RES | UEP_R_RES_ACK;
	}
}
