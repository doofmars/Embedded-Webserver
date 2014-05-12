﻿/*----------------------------------------------------------------------------
 Copyright:      Radig Ulrich  mailto: mail@ulrichradig.de
 Author:         Radig Ulrich
 Remarks:        
 known Problems: none
 Version:        24.10.2007
 Description:    Webserver Applikation
 Modified:       G. Menke, 05.08.2010

 Dieses Programm ist freie Software. Sie können es unter den Bedingungen der 
 GNU General Public License, wie von der Free Software Foundation veröffentlicht, 
 weitergeben und/oder modifizieren, entweder gemäß Version 2 der Lizenz oder 
 (nach Ihrer Option) jeder späteren Version. 

 Die Veröffentlichung dieses Programms erfolgt in der Hoffnung, 
 daß es Ihnen von Nutzen sein wird, aber OHNE IRGENDEINE GARANTIE, 
 sogar ohne die implizite Garantie der MARKTREIFE oder der VERWENDBARKEIT 
 FÜR EINEN BESTIMMTEN ZWECK. Details finden Sie in der GNU General Public License. 

 Sie sollten eine Kopie der GNU General Public License zusammen mit diesem 
 Programm erhalten haben. 
 Falls nicht, schreiben Sie an die Free Software Foundation, 
 Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA. 
------------------------------------------------------------------------------*/
#include "config.h"
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stack.h"
#include "base64.h"
#include "analog.h"
#include "wol.h"
#include "timer.h"
#include "usart.h"


#include "httpd.h"
#include "webpage.h"
#include "ntp.h"

struct http_table http_entry[MAX_TCP_ENTRY];

//Hier wird das codierte Passwort aus config.h gespeichert.
unsigned char http_auth_passwort[30];

unsigned char post_in[5] = {'S','E','T','='};
unsigned char post_ready[5] = {'S','U','B','='};
unsigned char PORTA_tmp = 0;
unsigned char PORTC_tmp = 0;
unsigned char PORTD_tmp = 0;
char dstr[24]={"No Time...             "};

//----------------------------------------------------------------------------
//Variablenarry zum einfügen in Webseite %VA@00 bis %VA@09
unsigned int var_array[MAX_VAR_ARRAY] = {10,50,30,2,0,0,0,0,0,0};
//----------------------------------------------------------------------------

//TEXT_PLAIN
const PROGMEM char http_header0[]={	"HTTP/1.0 200 Document follows\r\n"
								"Server: AVR_Small_Webserver\r\n"
								"Content-Type: text/plain\r\n\r\n"};
//TEXT_HTML
const PROGMEM char http_header1[]={	"HTTP/1.0 200 Document follows\r\n"
								"Server: AVR_Small_Webserver\r\n"
								"Content-Type: text/html\r\n\r\n"};
//TEXT_HTML_AUTH
const PROGMEM char http_header2[]={	"HTTP/1.0 401 Unauthorized\r\n"
								"Server: AVR_Small_Webserver\r\n"
								"WWW-Authenticate: Basic realm=\"NeedPassword\""
								"\r\nContent-Type: text/html\r\n\r\n"};
//IMAGE_SVG
const PROGMEM char http_header3[]={	"HTTP/1.0 200 Document follows\r\n"
								"Server: AVR_Small_Webserver\r\n"
								"Content-Type: image/svg+xml\r\n\r\n"};
//TEXT_JS						
const PROGMEM char http_header4[]={	"HTTP/1.0 200 Document follows\r\n"
								"Server: AVR_Small_Webserver\r\n"
								"Content-Type: text/javascript\r\n\r\n"};
//TEXT_CSS						
const PROGMEM char http_header5[]={	"HTTP/1.0 200 Document follows\r\n"
								"Server: AVR_Small_Webserver\r\n"
								"Content-Type: text/css\r\n\r\n"};



//----------------------------------------------------------------------------
//Kein Zugriff Seite bei keinem Passwort
const PROGMEM char Page0[] = {"401 Unauthorized%END"};

unsigned char rx_header_end[5] = {"\r\n\r\n\0"};

//----------------------------------------------------------------------------
//Initialisierung des Httpd Testservers
void httpd_init (void)
{
	//HTTP_AUTH_STRING 
	decode_base64((unsigned char*)HTTP_AUTH_STRING,http_auth_passwort);

	//Serverport und Anwendung eintragen
	add_tcp_app (HTTPD_PORT, (void(*)(unsigned char))httpd);
}
   
//----------------------------------------------------------------------------
//http Testserver
void httpd (unsigned char index)
{
    //Verbindung wurde abgebaut!
    if (tcp_entry[index].status & FIN_FLAG)
    {
        return;
    }

	//Allererste Aufruf des Ports für diese Anwendung
	//HTTPD_Anwendungsstack löschen
	if(tcp_entry[index].app_status==1)
	{
		httpd_stack_clear(index);
	}
	
	//HTTP wurde bei dieser Verbindung zum ersten mal aufgerufen oder
	//HTML Header Retransmission!
	if (tcp_entry[index].app_status <= 2)
	{	
		httpd_header_check (index);
		return;
	}
	
	//Der Header wurde gesendet und mit ACK bestätigt (tcp_entry[index].app_status+1)
	//war das HTML Packet fertig, oder müssen weitere Daten gesendet werden, oder Retransmission?
	if (tcp_entry[index].app_status > 2 && tcp_entry[index].app_status < 0xFFFE)
	{
		httpd_data_send (index);
		return;
	}
	
	//Verbindung kann geschlossen werden! Alle HTML Daten wurden gesendet TCP Port kann
	//geschlossen werden (tcp_entry[index].app_status >= 0xFFFE)!!
	if (tcp_entry[index].app_status >= 0xFFFE)
	{
		tcp_entry[index].app_status = 0xFFFE;
		tcp_Port_close(index);
		return;
	}
	return;
}

//----------------------------------------------------------------------------
//HTTPD_STACK löschen
void httpd_stack_clear (unsigned char index)
{
	http_entry[index].http_header_type = TEXT_HTML;
	http_entry[index].first_switch = 0;
	http_entry[index].http_auth = HTTP_AUTH_DEFAULT;
	http_entry[index].new_page_pointer = 0;
	http_entry[index].old_page_pointer = 0;
	http_entry[index].post = 0;
	http_entry[index].auth_ptr = http_auth_passwort;
	http_entry[index].post_ptr = post_in;
	http_entry[index].post_ready_ptr = post_ready;
	http_entry[index].hdr_end_pointer = rx_header_end;
	HTTP_DEBUG("\r\n**** NEUE HTTP ANFORDERUNG ****\r\n\r\n");	
	return;
}

//----------------------------------------------------------------------------
//Eintreffenden Header vom Client checken
void httpd_header_check (unsigned char index)
{
	unsigned int a = 0;
	
	if(strcasestr_P((char*)&eth_buffer[TCP_DATA_START_VAR],PSTR("POST"))!=0)
		{
		http_entry[index].post = 1;
		}
	
	//finden der Authorization und das Ende im Header auch über mehrere Packete hinweg!!	
	if(*http_entry[index].hdr_end_pointer != 0)
	{		
		for(a=TCP_DATA_START_VAR;a<(TCP_DATA_END_VAR);a++)
		{	
			HTTP_DEBUG("%c",eth_buffer[a]);
			
			if(!http_entry[index].http_auth) 
			{
				if (eth_buffer[a] != *http_entry[index].auth_ptr++)
				{
					http_entry[index].auth_ptr = http_auth_passwort;
				}
				if(*http_entry[index].auth_ptr == 0) 
				{
					http_entry[index].http_auth = 1;
					HTTP_DEBUG("  <---LOGIN OK!--->\r\n");
				}
			}
			
			if (eth_buffer[a] != *http_entry[index].hdr_end_pointer++)
			{
				http_entry[index].hdr_end_pointer = rx_header_end;
			}
			
			//Das Headerende wird mit (CR+LF+CR+LF) angezeigt!
			if(*http_entry[index].hdr_end_pointer == 0) 
			{
				HTTP_DEBUG("<---HEADER ENDE ERREICHT!--->\r\n");
				break;
			}
		}
	}
	
	//Einzelne Postpacket (z.B. bei firefox)
	if(http_entry[index].http_auth && http_entry[index].post == 1)
	{
		for(a = TCP_DATA_START_VAR;a<(TCP_DATA_END_VAR);a++)
		{	
			//Schaltanweisung finden!
			if (eth_buffer[a] != *http_entry[index].post_ptr++)
			{
				http_entry[index].post_ptr = post_in;
			}
			if(*http_entry[index].post_ptr == 0) 
			{

				unsigned char currPin  = (eth_buffer[a+2]-48);
				switch (eth_buffer[a+1])
				  {
				    case ('A'):
					  PORTA_tmp = PORTA_tmp + portToByte(currPin);
				      break;
					 
				//Port B is reserved for Ethernet
				
				    case ('C'):
					  PORTC_tmp = PORTC_tmp + portToByte(currPin);
				      break;

				    case ('D'):
					  PORTD_tmp = PORTD_tmp + portToByte(currPin);
				      break;

                    #if USE_WOL
                    case 'W':
                        wol_enable = 1;
						break;
                    #endif //USE_WOL
				  }
				http_entry[index].post_ptr = post_in;
				//Schaltanweisung wurde gefunden
			}
		
			//Submit schließt die suche ab!
			if (eth_buffer[a] != *http_entry[index].post_ready_ptr++)
			{
				http_entry[index].post_ready_ptr = post_ready;
			}
			if(*http_entry[index].post_ready_ptr == 0) 
			{
				http_entry[index].post = 0;
				PORTA = PORTA_tmp;
				PORTC = PORTC_tmp;
				PORTD = PORTD_tmp;
                PORTA_tmp = 0;
				PORTC_tmp = 0;
				PORTD_tmp = 0;
				break;
				//Submit gefunden
			}
		}
	}	
	
	//Welche datei wird angefordert? Wird diese in der Flashspeichertabelle gefunden?
	unsigned char page_index = 0;
	
	if (!http_entry[index].new_page_pointer)
	{
		for(a = TCP_DATA_START_VAR+5;a<(TCP_DATA_END_VAR);a++)
		{
			if (eth_buffer[a] == '\r')
			{
				eth_buffer[a] = '\0';
				break;
			}
		}
	
		// Initialize pointer to avoid problems when communcation is not finished after webpage transmssion
		http_entry[index].new_page_pointer = 0;

		while(WEBPAGE_TABLE[page_index].filename)
		{
			if (strcasestr((char*)&eth_buffer[TCP_DATA_START_VAR],WEBPAGE_TABLE[page_index].filename)!=0) 
				{
					HTTP_DEBUG("\r\n\r\nDatei gefunden: ");
					HTTP_DEBUG("%s",(char*)WEBPAGE_TABLE[page_index].filename);
					HTTP_DEBUG("<----------------\r\n\r\n");	
					
					if (strcasestr(WEBPAGE_TABLE[page_index].filename,".svg")!=0)
					{
						http_entry[index].http_header_type = IMAGE_SVG;
					}
					else if (strcasestr(WEBPAGE_TABLE[page_index].filename,".js")!=0)
					{
						http_entry[index].http_header_type = TEXT_JS;
					}	
					else if (strcasestr(WEBPAGE_TABLE[page_index].filename,".html")!=0)
					{
						http_entry[index].http_header_type = TEXT_HTML;	
					}
					else if (strcasestr(WEBPAGE_TABLE[page_index].filename,".css")!=0)
					{
						http_entry[index].http_header_type = TEXT_CSS;	
					}
					else
					{
						http_entry[index].http_header_type = TEXT_PLAIN;
					}
					
					http_entry[index].new_page_pointer = WEBPAGE_TABLE[page_index].page_pointer;
					break;
				}
			page_index++;
		}
	}

	//Wurde das Ende vom Header nicht erreicht
	//kommen noch weitere Stücke vom Header!
	if ((*http_entry[index].hdr_end_pointer != 0) || (http_entry[index].post == 1))
	{
		//Der Empfang wird Quitiert und es wird auf weiteres Headerstück gewartet
		tcp_entry[index].status =  ACK_FLAG;
		create_new_tcp_packet(0,index);
		//Warten auf weitere Headerpackete
		tcp_entry[index].app_status = 1;
		return;
	}	
	
	//Wurde das Passwort in den ganzen Headerpacketen gefunden?
	//Wenn nicht dann ausführen und Passwort anfordern!
	if((!http_entry[index].http_auth) && tcp_entry[index].status&PSH_FLAG)
	{	
		//HTTP_AUTH_Header senden!
		http_entry[index].new_page_pointer = Page0;
		memcpy_P((char*)&eth_buffer[TCP_DATA_START_VAR],http_header2,(sizeof(http_header2)-1));
		tcp_entry[index].status =  ACK_FLAG | PSH_FLAG;
		create_new_tcp_packet((sizeof(http_header2)-1),index);
		tcp_entry[index].app_status = 2;
		return;
	}
	
	//Standart INDEX.HTM Seite wenn keine andere gefunden wurde
	if (!http_entry[index].new_page_pointer)
	{
		//Besucher Counter
		var_array[MAX_VAR_ARRAY-1]++;
		
		http_entry[index].new_page_pointer = item_0;
		http_entry[index].http_header_type = TEXT_HTML;
	}	
	
	tcp_entry[index].app_status = 2;
	
	//Seiten Header wird gesendet
	switch(http_entry[index].http_header_type){
		case TEXT_PLAIN:
			memcpy_P((char*)&eth_buffer[TCP_DATA_START_VAR],http_header0,(sizeof(http_header0)-1));
			tcp_entry[index].status =  ACK_FLAG | PSH_FLAG;
			create_new_tcp_packet((sizeof(http_header0)-1),index);
		break;
		case TEXT_HTML:
			memcpy_P((char*)&eth_buffer[TCP_DATA_START_VAR],http_header1,(sizeof(http_header1)-1));
			tcp_entry[index].status =  ACK_FLAG | PSH_FLAG;
			create_new_tcp_packet((sizeof(http_header1)-1),index);
		break;
		case IMAGE_SVG:
			memcpy_P((char*)&eth_buffer[TCP_DATA_START_VAR],http_header3,(sizeof(http_header3)-1));
			tcp_entry[index].status =  ACK_FLAG | PSH_FLAG;
			create_new_tcp_packet((sizeof(http_header3)-1),index);
		break;
		case TEXT_JS:
			memcpy_P((char*)&eth_buffer[TCP_DATA_START_VAR],http_header4,(sizeof(http_header4)-1));
			tcp_entry[index].status =  ACK_FLAG | PSH_FLAG;
			create_new_tcp_packet((sizeof(http_header4)-1),index);
		break;
		case TEXT_CSS:
			memcpy_P((char*)&eth_buffer[TCP_DATA_START_VAR],http_header5,(sizeof(http_header5)-1));
			tcp_entry[index].status =  ACK_FLAG | PSH_FLAG;
			create_new_tcp_packet((sizeof(http_header5)-1),index);
		break;
	}
	return;
}

unsigned char portToByte(unsigned char port){
	switch (port)
	{
		case (0):
		return 1;
		
		case (1):
		return 2;
		
		case (2):
		return 4;
		
		case (3):
		return 8;
		
		case (4):
		return 16;
		
		case (5):
		return 32;
		
		case (6):
		return 64;
		
		case (7):
		return 128;
	}
}

//----------------------------------------------------------------------------
//Daten Packete an Client schicken
void httpd_data_send (unsigned char index)
{	
	unsigned int a;
	unsigned char str_len;
	
	char var_conversion_buffer[CONVERSION_BUFFER_LEN];
	
	//Passwort wurde im Header nicht gefunden
	if(!http_entry[index].http_auth)
	{
		http_entry[index].new_page_pointer = Page0;
	}
	//kein Packet empfangen Retransmission des alten Packetes
	if (tcp_entry[index].status == 0) 
	{
		http_entry[index].new_page_pointer = http_entry[index].old_page_pointer;
	}
	http_entry[index].old_page_pointer = http_entry[index].new_page_pointer;

	for (a = 0;a<(MTU_SIZE-(TCP_DATA_START)-150);a++)
	{
		unsigned char b;
		b = pgm_read_byte(http_entry[index].new_page_pointer++);
		eth_buffer[TCP_DATA_START + a] = b;
		
		//Müssen Variablen ins Packet eingesetzt werden? ===> %VA@00 bis %VA@07
		if (b == '%')
		{
			if (strncasecmp_P("VA@",http_entry[index].new_page_pointer,3)==0)
			{	
				b = (pgm_read_byte(http_entry[index].new_page_pointer+3)-48)*10;
				b +=(pgm_read_byte(http_entry[index].new_page_pointer+4)-48);	
				itoa (var_array[b],var_conversion_buffer,10);
				str_len = strnlen(var_conversion_buffer,CONVERSION_BUFFER_LEN);
				memmove(&eth_buffer[TCP_DATA_START+a],var_conversion_buffer,str_len);
				a = a + (str_len-1);
				http_entry[index].new_page_pointer=http_entry[index].new_page_pointer+5;
			}
			
            #if USE_NTP
            //Zeit in Webseite einfügen
            if (strncasecmp_P("TIME",http_entry[index].new_page_pointer,4)==0)
            {
                if (ntp_state == NTP_STATE_REQ_ERR)
                {
                    strcpy("dstr","-");
                }
                else
                {
                    ntp_struct dt;
                    decode_time(time, &dt);
                    sprintf_P(dstr, PSTR("%s, %s"), dt.datestr, dt.timestr);
                }
                str_len = strnlen(dstr,24);
                memmove(&eth_buffer[TCP_DATA_START+a],dstr,str_len);
                a += str_len - 1;
                http_entry[index].new_page_pointer=http_entry[index].new_page_pointer+4;
            }
            #endif
            
			//Einsetzen des Port Status %PORTxy durch "checked" wenn Portx.Piny = 1
			//x: A..G  y: 0..7 
			if (strncasecmp_P("PORT",http_entry[index].new_page_pointer,4)==0)
			{
				unsigned char pin  = (pgm_read_byte(http_entry[index].new_page_pointer+5)-48);	
				b = 0;
				switch(pgm_read_byte(http_entry[index].new_page_pointer+4))
				{
					case 'A':
						b = (PORTA & (1<<pin));
						break;
					case 'B':
						b = (PORTB & (1<<pin));
						break;
					case 'C':
						b = (PORTC & (1<<pin));
						break;
					case 'D':
						b = (PORTD & (1<<pin));
						break; 
				}
				
				if(b)
				{
					strcpy_P(var_conversion_buffer, PSTR("1"));
				}
				else
				{
					strcpy_P(var_conversion_buffer, PSTR("\0"));
				}
				str_len = strnlen(var_conversion_buffer,CONVERSION_BUFFER_LEN);
				memmove(&eth_buffer[TCP_DATA_START+a],var_conversion_buffer,str_len);
				a += str_len-1;
				http_entry[index].new_page_pointer = http_entry[index].new_page_pointer+6;
			}
			
			//Einsetzen des Pin Status %PINxy bis %PINxy durch "1" oder "0"
			//x = A : PINA / x = B : PINB / x = C : PINC / x = D : PIND
			if (strncasecmp_P("PIN",http_entry[index].new_page_pointer,3)==0)
			{
				unsigned char pin  = (pgm_read_byte(http_entry[index].new_page_pointer+4)-48);	
				b = 0;
				switch(pgm_read_byte(http_entry[index].new_page_pointer+3))
				{
					case 'A':
						b = (PINA & (1<<pin));
						break;
					case 'B':
						b = (PINB & (1<<pin));
						break;
					case 'C':
						b = (PINC & (1<<pin));
						break;
					case 'D':
						b = (PIND & (1<<pin));
						break;    
				}
				
				if(b)
				{
					strcpy_P(var_conversion_buffer, PSTR("1"));
				}
				else
				{
					strcpy_P(var_conversion_buffer, PSTR("0"));
				}
				str_len = strnlen(var_conversion_buffer,CONVERSION_BUFFER_LEN);
				memmove(&eth_buffer[TCP_DATA_START+a],var_conversion_buffer,str_len);
				a += str_len-1;
				http_entry[index].new_page_pointer = http_entry[index].new_page_pointer+5;
			}
			
			//Einsetzen des DDR Status %DDRxy bis %DDRxy durch "i" oder "o" für input oder output
			//x = A : PINA / x = B : PINB / x = C : PINC / x = D : PIND
			if (strncasecmp_P("DDR",http_entry[index].new_page_pointer,3)==0)
			{
				unsigned char pin  = (pgm_read_byte(http_entry[index].new_page_pointer+4)-48);	
				b = 0;
				switch(pgm_read_byte(http_entry[index].new_page_pointer+3))
				{
					case 'A':
						b = (DDRA & (1<<pin));
						break;
					case 'B':
						b = (DDRB & (1<<pin));
						break;
					case 'C':
						b = (DDRC & (1<<pin));
						break;
					case 'D':
						b = (DDRD & (1<<pin));
						break;    
				}
				
				if(b)
				{
					strcpy_P(var_conversion_buffer, PSTR("i"));
				}
				else
				{
					strcpy_P(var_conversion_buffer, PSTR("o"));
				}
				str_len = strnlen(var_conversion_buffer,CONVERSION_BUFFER_LEN);
				memmove(&eth_buffer[TCP_DATA_START+a],var_conversion_buffer,str_len);
				a += str_len-1;
				http_entry[index].new_page_pointer = http_entry[index].new_page_pointer+5;
			}
			//wurde das Ende des Packetes erreicht?
			//Verbindung TCP Port kann beim nächsten ACK geschlossen werden
			//Schleife wird abgebrochen keine Daten mehr!!
			if (strncasecmp_P("END",http_entry[index].new_page_pointer,3)==0)
			{	
				tcp_entry[index].app_status = 0xFFFD;
				break;
			}
		}
	}
	//Erzeugte Packet kann nun gesendet werden!
	tcp_entry[index].status =  ACK_FLAG | PSH_FLAG;
	create_new_tcp_packet(a,index);
	return;
}


