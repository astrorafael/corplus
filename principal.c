/******************************************************************************
 *
 * $Name: corplus-1v5 $
 * $Header: c:\\Repositorio/corplus/principal.c,v 1.3 2004/12/08 22:51:31 Astrorafael Exp $
 *
 * CREDITOS
 *
 * Version original de Cristobal García y Javier Pérez.
 * Version renovada de Rafael González Fuentetaja.
 *
 * Este fichero se distribuye bajo licencia de software abierto BSD
 * Ver fichero BSDLicen.txt adjunto.
 *
 * DESCRIPCION
 *
 * Este es el fichero principal que contiene:
 * - la inicializacion de todos los modulos
 * - el bucle principal de despacho de eventos en multitarea cooperativa
 * - las funciones de gestion de la 'conexion' al PC cliente.
 *
 * (No deja de ser chocante hablar de conexión utilizando un protocolo
 * de transporte que no hace conexiones)
 *
 * Funciones de red
 * ----------------
 *
 * Esta version esta preparada para hablar con distintos programas de
 * UN MISMO PC CLIENTE, como drivers ASCOM, el programa ACOR, driver CCD
 * de Astroart, etc.
 *
 * El unico requisito es que los clientes no se interfieran entre si. Si un
 * cliente maneja los controles de potencia del observatorio, no deberia haber
 * otros clientes haciendo lo mismo
 *
 * El Telescopio y la CCD principal se pueden manejar por clientes distintos.
 * El Telescopio y la CCD de autoguia se deben manejar por un mismo cliente.
 *
 * LIMITACIONES
 *
 * Esta por ver como se comportara el programa cuando dos clientes distintos
 * quieran hacer manejar cada uno su CCD (PPal y Guia). Creo que habra lio y
 * habra que estudiar mas a fondo la cuestion
 *
 ******************************************************************************/


#memmap xmem
#class auto

#use "protocolo.lib"                 /* definiciones de los mensajes del COR */
#if PROTOCOLO_COR_VERSION != 21
#error "Este programa solo soporta la version de protocolo COR 2.1"
#endif

#use "diagnosticos.lib"  /* Se debe importar antes de los perifericos */
#use "perifericos.lib"
#use "ccd.lib"

/* -------------------------------------------------------------------------- */

void red_cambia_ip (char* ipCOR, char* ipPC, char* maskCOR)
{
	auto char aux[15];
	extern PuntoDeRed local;
   extern udp_Socket sock;


	/* cerramos socket antes de cambiar propiedades de la interfaz */
	sock_close(&sock);

   /* tcp_config() está obsoleto */
   ifconfig(IF_ETH0, IFS_IPADDR, aton(ipCOR), IFS_NETMASK, aton(maskCOR), IFS_UP, IFS_END);

	/* reabrimos el socket para escuchar solo desde un PC. */
	if(!udp_open(&sock, local.puerto, inet_addr(ipPC), UDP_ANY_PORT, NULL)) {
		diag_putchar(DIAG_ERROR_OPEN);
	}

   // Hace tcp_tick() para resolver la direccion hardware,
   // de lo contrario, el proximo envio UDP fallará

   tcp_tick(NULL);
   
	diag_print("\n\r");
	diag_print(inet_ntoa(aux, gethostid()));
	diag_print("\n\r");
}

/* -------------------------------------------------------------------------- */

void red_envia_rabbit_vivo(char* respuesta)
{
	auto int perro;
	auto int* buffer;
	extern PuntoDeRed remitente[];
   extern udp_Socket sock;
   extern BufferGlobal bigbuf;

	perro = VdGetFreeWd(10);  //Parametro * 62.5 ms = 62 ms

	bigbuf.mensaje.saliente.cabecera.periferico = PERIF_COR;
	bigbuf.mensaje.saliente.cabecera.origen = ORIGEN_COR;
	strncpy(bigbuf.mensaje.saliente.cuerpo.confCnx.respuesta, respuesta, 24);
	bigbuf.mensaje.saliente.cuerpo.confCnx.relleno1[0] = 0;
	bigbuf.mensaje.saliente.cuerpo.confCnx.paso = SEQ_KCOL;

	/* Rellenar con las lecturas de los interruptorres optoacoplados */

	pwr_rellena(&bigbuf.mensaje.saliente.cuerpo.confCnx.potencia);

	/* Rellenar con las lecturas de los sensores de la Audine */

	ccd_carga_seq(&ccd[CCD_PPAL], CCD_SEQ_SENSORES);
	ccd_rellena_sensores(ccd, &bigbuf.mensaje.saliente.cuerpo.confCnx.tempCCD);

	/* fecha de compilacion */

	bigbuf.mensaje.saliente.cuerpo.confCnx.relleno2[8] = 0;
	strncpy(bigbuf.mensaje.saliente.cuerpo.confCnx.compila, __DATE__, 12);

	/* realizamos el envío a quien nos hizo la conexion */
	/* (y se supone que el mantenimiento).   */



	udp_sendto(&sock,
   		(char*) &bigbuf.mensaje.saliente,
			sizeof(Conf_Conexion_Msg)+sizeof bigbuf.mensaje.saliente.cabecera,
			remitente[REM_DEFECTO].ip,
			remitente[REM_DEFECTO].puerto);


	VdReleaseWd(perro);
}

/* -------------------------------------------------------------------------- */

void red_mantiene_conexion(void)
{
	auto int len;
	extern  BufferGlobal bigbuf;

	if (strncmp(bigbuf.mensaje.entrante.cuerpo.petMant.manten, MSG_CONTINUACION, 22) == 0) {
     		diag_putchar(DIAG_MANTEN);
     		red_envia_rabbit_vivo(MENS_RESP_CONEXION);
	}
}

/* -------------------------------------------------------------------------- */

int red_es_nueva_conexion(void)
{
	auto int len;
	char ipAddr[16];

	extern udp_Socket sock;
	extern BufferGlobal bigbuf;
	extern PuntoDeRed remitente[];

	len = udp_recvfrom(&sock, &bigbuf.mensaje.entrante,
							sizeof(Mensaje_Entrante),
							&remitente[REM_DEFECTO].ip,
		  					&remitente[REM_DEFECTO].puerto);

	if( (bigbuf.mensaje.entrante.cabecera.periferico == PERIF_COR) &&
       	    ! strncmp(bigbuf.mensaje.entrante.cuerpo.petCnx.solicitud, MSG_SOLICITUD, 24))  {


     red_cambia_ip( bigbuf.mensaje.entrante.cuerpo.petCnx.ipCOR,
      				  bigbuf.mensaje.entrante.cuerpo.petCnx.ipPC,
						  bigbuf.mensaje.entrante.cuerpo.petCnx.maskCOR);

		diag_putchar(DIAG_CONEXION);
		red_envia_rabbit_vivo(MENS_RESP_CONEXION);
		return (1);
	}

   return (0);
}

/* -------------------------------------------------------------------------- */

cofunc void procesa_peticion(void)
{
	extern BufferGlobal bigbuf;
	extern PuntoDeRed remitente[];
   extern udp_Socket sock;
	auto int len;
	auto unsigned long remIP;
	auto unsigned int  remPuerto;

   len = udp_recvfrom(&sock, &bigbuf.mensaje.entrante,
   						sizeof(Mensaje_Entrante), &remIP, &remPuerto);

	if (len<0)  {
		diag_putchar(DIAG_ERROR_UDP);
		return;
	}

	len -= sizeof bigbuf.mensaje.entrante.cabecera;	/* quita los bytes de cabecera comun */

	switch(bigbuf.mensaje.entrante.cabecera.periferico) {

		case PERIF_COR:
			remitente[REM_DEFECTO].ip = remIP;
			remitente[REM_DEFECTO].puerto = remPuerto;
			red_mantiene_conexion();
			break;

      case PERIF_POWER:
			remitente[REM_POTENCIA].ip = remIP;
			remitente[REM_POTENCIA].puerto = remPuerto;
			diag_putchar(DIAG_POWER);
			pwr_activa();
			pwr_lee_sensores();
			break;

      case PERIF_CCD_PPAL:
			remitente[REM_PERIF_CCD_PPAL].ip = remIP;
			remitente[REM_PERIF_CCD_PPAL].puerto = remPuerto;
			if(bigbuf.mensaje.entrante.cuerpo.petFoto.cancelar) {
				diag_putchar(DIAG_CANCEL_PPAL);
				ccd_cancelaFoto(&ccd[CCD_PPAL]);
			} else {
				diag_putchar(DIAG_FOTO_PPAL);
				ccd_foto(&ccd[CCD_PPAL]);
			}
			break;

		case PERIF_CCD_GUIA:
			remitente[REM_PERIF_CCD_GUIA].ip = remIP;
			remitente[REM_PERIF_CCD_GUIA].puerto = remPuerto;
			if(bigbuf.mensaje.entrante.cuerpo.petFoto.cancelar) {
				diag_putchar(DIAG_CANCEL_GUIA);
				ccd_cancelaFoto(&ccd[CCD_GUIA]);
			} else {
				diag_putchar(DIAG_FOTO_GUIA);
				ccd_foto(&ccd[CCD_GUIA]);
			}
			break;

#if DEBUG == 0
		case PERIF_SER_A:
			remitente[REM_PERIF_SER_A].ip = remIP;
			remitente[REM_PERIF_SER_A].puerto = remPuerto;
			diag_putchar(DIAG_SER_A);
         waitfordone perif_envia_puerto_B(len);
			break;
#endif
		case PERIF_SER_B:
			remitente[REM_PERIF_SER_B].ip = remIP;
			remitente[REM_PERIF_SER_B].puerto = remPuerto;
			diag_putchar(DIAG_SER_B);
			waitfordone perif_envia_puerto_B(len);
			break;

		case PERIF_SER_C:
			remitente[REM_PERIF_SER_C].ip = remIP;
			remitente[REM_PERIF_SER_C].puerto = remPuerto;
			diag_putchar(DIAG_SER_C);
			waitfordone perif_envia_puerto_C(len);
			break;

		case PERIF_SER_D:
			remitente[REM_PERIF_SER_D].ip = remIP;
			remitente[REM_PERIF_SER_D].puerto = remPuerto;
			diag_putchar(DIAG_SER_D);
			waitfordone perif_envia_puerto_D(len);
			break;

		case PERIF_PRU:	/* quien deberia ser el remitente de esto? */
			diag_putchar(DIAG_TEST);
			break;

		case PERIF_RESET:
			diag_putchar(DIAG_RESET);
			waitfor(DelayMs(500));
			forceSoftReset();
			break;

		default:
			diag_putchar(DIAG_DESCONO);
	}
}

/* -------------------------------------------------------------------------- */

void inicializa_todo(void)
{
	extern int  PC_Conectado;
	extern  udp_Socket sock;
	auto char ip[16];

	/* Configuración del PCLK (Peripheal Clock) */
	WrPortI(GOCR,&GOCRShadow,(GOCRShadow & 0x3F));

	loopinit();    /* para las cofunctions cof_serXread */
	perif_init();  /* perifericos de puerto serie */
   pwr_init();    /* inicializa el modulo de potencia */
	xil_download();    /* carga la Xilinx */
	ccd_init();    /* y el modulo CCD */

   diag_print("$Name: corplus-1v5 $ "); /* imprime etiqueta de release */
   diag_print(__DATE__);	 /* imprime la fecha de compilación */
	diag_print("\n\r");

	/* Inicializacion de red */

	PC_Conectado = 0;
	local.ip = inet_addr(IP_INICIAL);
	local.puerto = UDP_LOCAL_PORT;
	sock_init();	/* inicializa la pila UDP/IP */

	/* Se abre un puert UDP escuchando en broadcast */
	if(!udp_open(&sock, UDP_LOCAL_PORT, BROADCAST_IP, 0, NULL)) {
		diag_putchar(DIAG_ERROR_OPEN);
		exit(0);
	}

 	diag_print(inet_ntoa(ip, gethostid()));
	diag_print("\n\r");
}

/**************************************************************************/
/*                    BLOQUE DE VARIABLES GLOBALES                        */
/**************************************************************************/

PuntoDeRed   remitente[NUM_REMITENTES];
PuntoDeRed   local;
udp_Socket   sock;
BufferGlobal bigbuf;
int PC_Conectado;
unsigned long tUltimaActividad;

/**************************************************************************/
/*                          PROGRAMA PRINCIPAL                            */
/**************************************************************************/

void main()
{
	extern int PC_Conectado;
	extern unsigned long tUltimaActividad; 	/* La hago externa para no comer memoria de stack */

	extern udp_Socket sock;
	extern CCD  ccd[];


	inicializa_todo();


	/****************************************/
	/* Bucle infinito de tareas en paralelo */
	/****************************************/

	for(;;) {

		loophead();      /* necesario para las 'single user cofunctions' */

	/***************************************************/
	/* Tarea de atencion a los tiempos de exposicion y */
	/* lectura de las camaras CCD. Tambien atiende la  */
	/* pila de protocolos TCP/IP                       */
	/***************************************************/

		costate {
			tcp_tick(NULL);  /* Atender al UDP/IP  */
         if(ccd_timeout(&ccd[CCD_PPAL]) || ccd_timeout(&ccd[CCD_GUIA])) {
         	tUltimaActividad = SEC_TIMER;
			}
		}

      /********************************************/
      /* Tareas de atencion a los puertos serie.  */
      /********************************************/

#if DEBUG == 0
      costate {
      	waitfordone perif_escucha_puerto_A();
      }
#endif
      costate {
      	waitfordone perif_escucha_puerto_B();
      }

       costate {
      	waitfordone perif_escucha_puerto_C();
      }

       costate {
      	waitfordone perif_escucha_puerto_D();
      }

	/************************************************/
	/* Tarea de atencion a la red por el socket UDP */
	/************************************************/

		costate {
			waitfor(udp_peek(&sock, NULL) == 1);
			if(!PC_Conectado && red_es_nueva_conexion()) {
				PC_Conectado = 1;
			} else {
				waitfordone procesa_peticion();
				tUltimaActividad = SEC_TIMER;	// toma un timestamp de ultima actividad
			}
		}

      /**********************************************************/
      /* Tarea periodica que o bien lanza mensajes de presencia */
      /* o monitoriza el tiempo del ultimo mensaje para pasar a */
      /* estado de escucha en broadcast                         */
      /**********************************************************/

		costate {
			if(!PC_Conectado) {
				diag_putchar(DIAG_ESPERA);
				red_envia_rabbit_vivo(MENS_PRESENCIA);
			} else if ((SEC_TIMER - tUltimaActividad) > 5) {
				red_cambia_ip(IP_INICIAL, "255.255.255.255", MASCARA_INICIAL);
				PC_Conectado = 0;
			}
			waitfor(DelaySec(2));
		}

   }  /* bucle infinito */
}  /* main */

/******************************************************************************
 *
 * $Log: principal.c,v $
 * Revision 1.3  2004/12/08 22:51:31  Astrorafael
 * añadido tcp_tick() a red_cambia_ip() porque no se esperaba
 * el tiempo suficiente para resolver la dirección hardware.
 * Eso causa que el protocolo COR falle parcialmente al principio y
 * no se envien mensajes de control de conexión.
 *
 * Revision 1.2  2004/09/27 22:03:23  Astrorafael
 * Añadida keyword CVS con el nombre de la release
 *
 * Revision 1.1.1.1  2004/09/22 22:44:20  Astrorafael
 * Version inicial
 *
 *
 ******************************************************************************/

