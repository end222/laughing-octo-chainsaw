/****************************************************************************/
/* Plantilla para implementación de funciones del cliente (rcftpclient)     */
/* $Revision: 1.6 $ */
/* Aunque se permite la modificación de cualquier parte del código, se */
/* recomienda modificar solamente este fichero y su fichero de cabeceras asociado. */
/****************************************************************************/

#define F_VERBOSE       0x1
#define S_SYSERROR 2
#define ANSI_COLOR_RESET   "\x1b[0m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"

/**************************************************************************/
/* INCLUDES                                                               */
/**************************************************************************/
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <stdbool.h>
#include <signal.h>
#include "rcftp.h" // Protocolo RCFTP
#include "rcftpclient.h" // Funciones ya implementadas
#include "multialarm.h" // Gestión de timeouts
#include "vemision.h" // Gestión de ventana de emisión
#include "misfunciones.h"

/**************************************************************************/
/* VARIABLES GLOBALES                                                     */
/**************************************************************************/
// elegir 1 o 2 autores y sustituir "Apellidos, Nombre" manteniendo el formato
//char* autores="Autor: Apellidos, Nombre"; // un solo autor
char* autores="Autor: Orduna Lagarma, Pablo\nAutor: Naval Alcala, Daniel"; // dos autores

// variable para indicar si mostrar información extra durante la ejecución
// como la mayoría de las funciones necesitaran consultarla, la definimos global
extern char verb;


// variable externa que muestra el número de timeouts vencidos
// Uso: Comparar con otra variable inicializada a 0; si son distintas, tratar un timeout e incrementar en uno la otra variable
extern volatile const int timeouts_vencidos;


/**************************************************************************/
/* Obtiene la estructura de direcciones del servidor */
/**************************************************************************/
struct addrinfo* obtener_struct_direccion(char *dir_servidor, char *servicio, char f_verbose){
	struct addrinfo hints, //estructura hints para especificar la solicitud
				*servinfo; // puntero al addrinfo devuelto
	int status; // indica la finalización correcta o no de la llamada getaddrinfo
	int numdir=1; // contador de estructuras de direcciones en la lista de direcciones de servinfo
	struct addrinfo *direccion; // puntero para recorrer la lista de direcciones de servinfo

	// genera una estructura de dirección con especificaciones de la solicitud
	if (f_verbose) printf("1 - Especificando detalles de la estructura de direcciones a solicitar... \n");
	// sobreescribimos con ceros la estructura para borrar cualquier dato que pueda malinterpretarse
	memset(&hints, 0, sizeof hints); 
   
	if (f_verbose) { printf("\tFamilia de direcciones/protocolos: "); fflush(stdout);}
	hints.ai_family=AF_UNSPEC; // sin especificar: AF_UNSPEC; IPv4: AF_INET; IPv6: AF_INET6; etc
	if (f_verbose) { 
		switch (hints.ai_family) {
			case AF_UNSPEC: printf("IPv4 e IPv6\n"); break;
			case AF_INET: printf("IPv4)\n"); break;
			case AF_INET6: printf("IPv6)\n"); break;
			default: printf("No IP (%d)\n",hints.ai_family); break;
		}
	}
   
	if (f_verbose) { printf("\tTipo de comunicacion: "); fflush(stdout);}
	hints.ai_socktype=SOCK_DGRAM;// especificar tipo de socket 
	if (f_verbose) { 
		switch (hints.ai_socktype) {
			case SOCK_STREAM: printf("flujo (TCP)\n"); break;
			case SOCK_DGRAM: printf("datagrama (UDP)\n"); break;
			default: printf("no convencional (%d)\n",hints.ai_socktype); break;
		}
	}
   
	// pone flags específicos dependiendo de si queremos la dirección como cliente o como servidor
	if (dir_servidor!=NULL) {
		// si hemos especificado dir_servidor, es que somos el cliente y vamos a conectarnos con dir_servidor
		if (f_verbose) printf("\tNombre/direccion del equipo: %s\n",dir_servidor); 
	} else {
		// si no hemos especificado, es que vamos a ser el servidor
		if (f_verbose) printf("\tNombre/direccion del equipo: ninguno (seremos el servidor)\n"); 
		hints.ai_flags=AI_PASSIVE; // poner flag para que la IP se rellene con lo necesario para hacer bind
	}
	if (f_verbose) printf("\tServicio/puerto: %s\n",servicio);
	

	// llamada a getaddrinfo para obtener la estructura de direcciones solicitada
	// getaddrinfo pide memoria dinámica al SO, la rellena con la estructura de direcciones, y escribe en servinfo la dirección donde se encuentra dicha estructura
	// la memoria *dinámica* creada dentro de una función NO se destruye al salir de ella. Para liberar esta memoria, usar freeaddrinfo()
	if (f_verbose) { printf("2 - Solicitando la estructura de direcciones con getaddrinfo()... "); fflush(stdout);}
	status = getaddrinfo(dir_servidor,servicio,&hints,&servinfo);
	if (status!=0) {
		fprintf(stderr,"Error en la llamada getaddrinfo: %s\n",gai_strerror(status));
		exit(1);
	} 
	if (f_verbose) { printf("hecho\n"); }


	// imprime la estructura de direcciones devuelta por getaddrinfo()
	if (f_verbose) {
		printf("3 - Analizando estructura de direcciones devuelta... \n");
		direccion=servinfo;
		while (direccion!=NULL) { // bucle que recorre la lista de direcciones
			printf("    Direccion %d:\n",numdir);
			printsockaddr((struct sockaddr_storage*)direccion->ai_addr);
			// "avanzamos" direccion a la siguiente estructura de direccion
			direccion=direccion->ai_next;
			numdir++;
		}
	}

	// devuelve la estructura de direcciones devuelta por getaddrinfo()
	return servinfo;
}

/**************************************************************************/
/* Imprime una direccion */
/**************************************************************************/
void printsockaddr(struct sockaddr_storage * saddr) {
	struct sockaddr_in  *saddr_ipv4; // puntero a estructura de dirección IPv4
	// el compilador interpretará lo apuntado como estructura de dirección IPv4
	struct sockaddr_in6 *saddr_ipv6; // puntero a estructura de dirección IPv6
	// el compilador interpretará lo apuntado como estructura de dirección IPv6
	void *addr; // puntero a dirección. Como puede ser tipo IPv4 o IPv6 no queremos que el compilador la interprete de alguna forma particular, por eso void
	char ipstr[INET6_ADDRSTRLEN]; // string para la dirección en formato texto
	int port; // para almacenar el número de puerto al analizar estructura devuelta

	if (saddr==NULL) { 
		printf("La dirección está vacia\n");
	} else {
		printf("\tFamilia de direcciones: "); fflush(stdout);
		if (saddr->ss_family == AF_INET6) { //IPv6
			printf("IPv6\n");
			// apuntamos a la estructura con saddr_ipv6 (el typecast evita el warning), así podemos acceder al resto de campos a través de este puntero sin más typecasts
			saddr_ipv6=(struct sockaddr_in6 *)saddr;
			// apuntamos a donde está realmente la dirección dentro de la estructura
			addr = &(saddr_ipv6->sin6_addr);
			// obtenemos el puerto, pasando del formato de red al formato local
			port = ntohs(saddr_ipv6->sin6_port);
		} else if (saddr->ss_family == AF_INET) { //IPv4
			printf("IPv4\n");
			saddr_ipv4 = (struct sockaddr_in *)saddr;
			addr = &(saddr_ipv4->sin_addr);
			port = ntohs(saddr_ipv4->sin_port);
		} else {
			fprintf(stderr, "familia desconocida\n");
			exit(1);
		}
		//convierte la dirección ip a string 
		inet_ntop(saddr->ss_family, addr, ipstr, sizeof ipstr);
		printf("\tDireccion (interpretada segun familia): %s\n", ipstr);
		printf("\tPuerto (formato local): %d \n", port);
	}
}

/**************************************************************************/
/* Configura el socket, devuelve el socket y servinfo */
/**************************************************************************/
int initsocket(struct addrinfo *servinfo, char f_verbose){
	int sock;

	printf("\nSe usara UNICAMENTE la primera direccion de la estructura\n");

	//crea un extremo de la comunicacion y devuelve un descriptor
	if (f_verbose) { printf("Creando el socket (socket)... "); fflush(stdout); }
	sock = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
	if (sock < 0) {
		perror("Error en la llamada socket: No se pudo crear el socket");
		/*muestra por pantalla el valor de la cadena suministrada por el programador, dos puntos y un mensaje de error que detalla la causa del error cometido */
		exit(1);
	}
	if (f_verbose) { printf("hecho\n"); }

	//inicia una conexion en el socket:
	//	if (f_verbose) { printf("Estableciendo la comunicacion a traves del socket (connect)... "); fflush(stdout); }
	//	if (connect(sock, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
	//		perror("Error en la llamada connect: No se pudo conectar con el destino");
	//		exit(1);
	//	} 
	if(f_verbose){ printf("hecho\n"); }

	return sock;
}

/*
 * Funcion dedicada a la construccion y declaracion de variables del mensaje
 */

struct rcftp_msg construirMensajeRCFTP(int numseq, int longitud, char * buffer, bool esUltimo, struct rcftp_msg mensaje){

	mensaje.version = RCFTP_VERSION_1;
	mensaje.len = htons(longitud);

	mensaje.next = htonl(0);
	mensaje.numseq = htonl(numseq);
	mensaje.sum = 0;

	// Comprobamos si es el ultimo mensaje para poner los flags correctamente
	if(esUltimo){
		mensaje.flags = F_FIN;
	}
	else{
		mensaje.flags = F_NOFLAGS;
	}
	return mensaje;
}

void enviar(int s, struct rcftp_msg sendbuffer, struct sockaddr *remote, socklen_t remotelen, unsigned int flags) {
        ssize_t sentsize = sendto(s,(char *)&sendbuffer,sizeof(sendbuffer),0,remote,remotelen);
        if (sentsize != sizeof(sendbuffer)) {
                if (sentsize!=-1)
                        fprintf(stderr,"Error: enviados %d bytes de un mensaje de %d bytes\n",(int)sentsize,(int)sizeof(sendbuffer));
                else
                        perror("Error en sendto");
                exit(S_SYSERROR);
        }
        // print response if in verbose mode
        if (flags & F_VERBOSE) {
                printf("Mensaje RCFTP " ANSI_COLOR_MAGENTA "enviado" ANSI_COLOR_RESET ":\n");
                print_rcftp_msg(&sendbuffer,sizeof(sendbuffer));
        }
}

/*
 * La funcion realiza la recepcion del mensaje que transmite el servidor
 */

ssize_t recibir(int socket, struct rcftp_msg *buffer, int buflen, struct sockaddr *remote, socklen_t *remotelen) {
        ssize_t recvsize;

        *remotelen = sizeof(*remote);
        recvsize = recvfrom(socket,(char *)buffer,buflen,0,remote,remotelen);
        if (recvsize<0 && errno!=EAGAIN) { // en caso de socket no bloqueante
                //if (recvsize<0 && errno!=EINTR) { // en caso de socket bloqueante (no funciona en GNU/Linux)
                perror("Error en recvfrom: ");
                exit(S_SYSERROR);
        } else if (*remotelen>sizeof(*remote)) {
                fprintf(stderr,"Error: la dirección del cliente ha sido truncada\n");
                exit(S_SYSERROR);
        }
        return recvsize;
}

/*
 * Funcion dedicada a la comprobacion de la validez del mensaje recibido (Checksum y version)
 */

int esMensajeValido(struct rcftp_msg recvbuffer) {
        int esperado=1;
	// Comprueba la version
        if (recvbuffer.version!=RCFTP_VERSION_1) { // versión incorrecta
                esperado=0;
                fprintf(stderr,"Error: recibido un mensaje con versión incorrecta\n");
        }
	// Comprueba el checksum
        if (issumvalid(&recvbuffer,sizeof(recvbuffer))==0) { // checksum incorrecto
                esperado=0;
                fprintf(stderr,"Error: recibido un mensaje con checksum incorrecto\n (esperaba ");
                //recvbuffer.sum=0;
                //fprintf(stderr,"0x%x)\n",ntohs(xsum((char*)&recvbuffer,sizeof(recvbuffer))));
        }
        return esperado;
}

/*
 * Funcion que realiza diferentes comprobaciones referentes a los flags del mensaje y la respuesta para
 * saber si es la respuesta esperada
 */

bool esLaRespuestaEsperada(struct rcftp_msg mensaje, struct rcftp_msg respuesta){
	// Se comprueba que mensaje y respuesta tiene simultaneamente activado el flag fin, ademas de que no haya flags de ocupado/abortar
	// en respuesta.
	if((respuesta.flags/F_BUSY)%2!=1 && (respuesta.flags/F_ABORT)%2!=1 && (respuesta.flags/F_FIN)%2==(mensaje.flags/F_FIN)%2){
		//Se comprueba si respuesta.next es igual a mensaje.numseq + mensaje.len
		return htonl(respuesta.next) == htonl(mensaje.numseq) + htons(mensaje.len);
	}
	else {
		return false;
	}
}

bool esLaRespuestaEsperadaVentana(struct rcftp_msg mensaje, struct rcftp_msg respuesta){
	if((respuesta.flags/F_BUSY)%2!=1 && (respuesta.flags/F_ABORT)%2!=1){
		return perteneceVentana(ntohl(respuesta.next));
	}
	else {
		return false;
	}
}
/**************************************************************************/
/*  algoritmo 1 (basico)  */
/**************************************************************************/
void alg_basico(int socket, struct addrinfo *servinfo) {

	printf("Comunicación con algoritmo básico\n");

	char buffer[RCFTP_BUFLEN];
	int numseq = 0;
	bool ultimoMensaje = false;
	bool ultimoMensajeConfirmado = false;
	struct rcftp_msg mensaje;

	int longitud = readtobuffer((char *)mensaje.buffer, RCFTP_BUFLEN);

	// Comprobamos que no se ha mandado un mensaje vacio
	if (longitud == 0){
		ultimoMensaje = true;
	}

	// Se construye el mensaje
	mensaje = construirMensajeRCFTP(numseq, longitud, buffer, ultimoMensaje, mensaje);
	numseq = numseq + longitud;
	struct rcftp_msg respuesta;
	struct sockaddr remote;
	socklen_t remotelen = 0;
	mensaje.sum = xsum((char*)&mensaje, sizeof(struct rcftp_msg));

	// Bucle de enviar/recibir
	while(!ultimoMensajeConfirmado){
		enviar(socket, mensaje, servinfo->ai_addr, servinfo->ai_addrlen, servinfo->ai_flags);
		recibir(socket, &respuesta, sizeof(respuesta), &remote, &remotelen);

		if(esMensajeValido(respuesta) && esLaRespuestaEsperada(mensaje, respuesta)){

			// Se ha cumplido que el mensaje es valido y la respuesta es la esperada
			if(ultimoMensaje){

				// Ha llegado el ultimo mensaje 
				ultimoMensajeConfirmado = true;
			}
			else{

				// No es el ultimo y, por tanto, se construye el siguiente
				int longitud = readtobuffer((char *)mensaje.buffer, RCFTP_BUFLEN);

				// Comprobamos que no se ha mandado un mensaje vacio
				if(longitud == 0){
					ultimoMensaje = true;
				}

				mensaje = construirMensajeRCFTP(numseq, longitud, buffer, ultimoMensaje, mensaje);
				mensaje.sum = xsum((char*)&mensaje, sizeof(struct rcftp_msg));
				
				// Calculamos el siguiente numero de secuencia 
				numseq = numseq + longitud;
			}
		}

	}
}

/**************************************************************************/
/*  algoritmo 2 (stop & wait)  */
/**************************************************************************/
void alg_stopwait(int socket, struct addrinfo *servinfo) {

	signal(SIGALRM,handle_sigalrm);

	int sockflags;
	sockflags = fcntl (socket, F_GETFL, 0);
	fcntl(socket, F_SETFL, sockflags | O_NONBLOCK);
	printf("Comunicación con algoritmo stop&wait\n");
	char buffer[RCFTP_BUFLEN];
	int numseq = 0;
	bool ultimoMensaje = false;
	bool ultimoMensajeConfirmado = false;
	struct rcftp_msg mensaje;

	int longitud = readtobuffer((char *)mensaje.buffer, RCFTP_BUFLEN);

	// Comprobamos que no se ha mandado un mensaje vacio
	if (longitud == 0){
		ultimoMensaje = true;
	}

	// Se construye el mensaje
	mensaje = construirMensajeRCFTP(numseq, longitud, buffer, ultimoMensaje, mensaje);
	numseq = numseq + longitud;
	struct rcftp_msg respuesta;
	struct sockaddr remote;
	socklen_t remotelen = 0;
	mensaje.sum = xsum((char*)&mensaje, sizeof(struct rcftp_msg));

	// Bucle de enviar/recibir
	while(!ultimoMensajeConfirmado){
		enviar(socket, mensaje, servinfo->ai_addr, servinfo->ai_addrlen, servinfo->ai_flags);
		
		addtimeout();
		
		int tamano = 0;
		int contador = timeouts_vencidos;

		while(tamano == 0 && contador == timeouts_vencidos){
			tamano = recibir(socket, &respuesta, sizeof(respuesta), &remote, &remotelen);
		}

		if(tamano > 0){
			canceltimeout();
		}

		if(esMensajeValido(respuesta) && esLaRespuestaEsperada(mensaje, respuesta) && tamano > 0){

			// Se ha cumplido que el mensaje es valido y la respuesta es la esperada
			if(ultimoMensaje){

				// Ha llegado el ultimo mensaje 
				ultimoMensajeConfirmado = true;
			}
			else{

				// No es el ultimo y, por tanto, se construye el siguiente
				int longitud = readtobuffer((char *)mensaje.buffer, RCFTP_BUFLEN);

				// Comprobamos que no se ha mandado un mensaje vacio
				if(longitud == 0){
					ultimoMensaje = true;
				}

				mensaje = construirMensajeRCFTP(numseq, longitud, buffer, ultimoMensaje, mensaje);
				mensaje.sum = xsum((char*)&mensaje, sizeof(struct rcftp_msg));
				
				// Calculamos el siguiente numero de secuencia 
				numseq = numseq + longitud;
			}
		}

	}
}

/**************************************************************************/
/*  algoritmo 3 (ventana deslizante)  */
/**************************************************************************/
void alg_ventana(int socket, struct addrinfo *servinfo,int window) {

	signal(SIGALRM,handle_sigalrm);
	
	int sockflags;
	sockflags = fcntl (socket, F_GETFL, 0);
	fcntl(socket, F_SETFL, sockflags | O_NONBLOCK);

	setwindowsize(window);
	printf("Comunicación con algoritmo go-back-n\n");
	bool ultimoMensaje = false;
	bool ultimoMensajeConfirmado = false;
	int tamanoRecv;
	int longitud;
	char buffer[RCFTP_BUFLEN];
	int numseq = 0;
	struct rcftp_msg mensaje;
	struct rcftp_msg respuesta;
	struct sockaddr remote;
	socklen_t remotelen = 0;
	int timeouts_procesados = 0;
	while(ultimoMensajeConfirmado == false){
		if(getfreespace() >= RCFTP_BUFLEN && !ultimoMensaje){
			longitud = readtobuffer((char *)mensaje.buffer, RCFTP_BUFLEN);

			if (longitud == 0){
				ultimoMensaje = true;
			}
			
			mensaje = construirMensajeRCFTP(numseq, longitud, buffer, ultimoMensaje, mensaje);
			mensaje.sum = xsum((char*)&mensaje, sizeof(struct rcftp_msg));
			numseq = numseq + longitud;
			
			enviar(socket, mensaje, servinfo->ai_addr, servinfo->ai_addrlen, servinfo->ai_flags);
			addtimeout();
			addsentdatatowindow((char *)mensaje.buffer,longitud);
		}

		tamanoRecv = recibir(socket, &respuesta, sizeof(respuesta), &remote, &remotelen);
		
		if(tamanoRecv > 0){
			
			if(esMensajeValido(respuesta) && esLaRespuestaEsperadaVentana(mensaje, respuesta)){
				canceltimeout();
				freewindow(ntohl(respuesta.next));
				
				if(ultimoMensaje && respuesta.flags == mensaje.flags){
					printf("d\n");
					ultimoMensajeConfirmado = true;
				}
			}
			if(respuesta.len == 0 && respuesta.flags == F_FIN){
				canceltimeout();
				ultimoMensajeConfirmado = true;
			}
		}

		printf("%d\n",getnumtimeouts());
		printf("Timeouts: %d Procesados: %d\n",timeouts_vencidos, timeouts_procesados);
		printf("EspacioLibre: %d\n",getfreespace());
		printvemision();	
		if(timeouts_procesados != timeouts_vencidos){
			timeouts_procesados++;
			int max = RCFTP_BUFLEN;
			int numseqResend = getdatatoresend((char *)mensaje.buffer,&max);
		
			mensaje = construirMensajeRCFTP(numseqResend, longitud, buffer, ultimoMensaje, mensaje);
			mensaje.sum = xsum((char*)&mensaje, sizeof(struct rcftp_msg));

			enviar(socket, mensaje, servinfo->ai_addr, servinfo->ai_addrlen, servinfo->ai_flags);
			addtimeout();
		}
	}
}
