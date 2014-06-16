 
// Código para Arduino. Hace de puente entre el PC anfitrión y los LEDs RGB direccionables basados en el IC LPD6803
// Estos son, por ejemplo los que se pueden comprar en Dealextreme
 
#include <TimerOne.h>
#include <LPD6803.h>
 
// Una 'palabra mágica' precede cada bloque de datos LED; esto ayuda al microcontrolador a sincronizar con el software del PC anfitrion y a presentar
// los cuadros (frames) en el momento justo. Es posible ver uno o dos cuadros erraticos hasta que los leds se alineen. Inmediatamente despues de la
// 'palabra mágica' van 3 bytes: un conteo de los LEDs de 16-bit (el byte mas alto primero) seguido por un valor de checksum simple (high byte XOR low 
// byte XOR 0x55).  Despues se envian los datos de cada LED, 3 bytes per LED, en orden R, G, B, donde 0 = off y 255 = brillo máximo.
 
static const uint8_t magic[] = { 'A','d','a' };
#define MAGICSIZE  sizeof(magic)
#define HEADERSIZE (MAGICSIZE + 3)
static uint8_t
  buffer[HEADERSIZE], // Serial input buffer
  bytesBuffered = 0;  // Cantidad de datos en buffer
 
 
int dataPin = 11;
int clockPin = 13;
int r,g,b;
int RGB[3];
 
 
LPD6803 strip = LPD6803(50,dataPin,clockPin);     //Se inicializa la tira de LEDS. Los parámetros son: Número de LEDS, DataPin, ClockPin
 
static const unsigned long serialTimeout = 10000; // 10 segundos
static unsigned long       lastByteTime, lastAckTime;
 
 
//---------------------------SETUP---------------------------//
void setup(){
  byte c;
  int i,p;
  
  Serial.begin(115200);
  
  strip.setCPUmax(50);    //Establecemos la frecuencia máxima de la CPU.
  strip.begin();
  
  strip.show();
  
  const uint8_t testColor[]  = { 0x80, 0x80, 0xff, 0x80, 0x80, 0x80 },
                testOffset[] = { 1, 2, 0, 3 };
 
  // Esto es una prueba de los LEDS para verificar que el cableado entre el Arduino y los LEDs es correcto.
  // Si el cableado es correcto los LEDs se encenderán en Rojo, luego Verde y luego Azul. Finalmente se apagarán. 
  // Una vez se compruebe que todo está correcto, se aconseja comentar estas líneas y volver a subir el código al Arduino          
               
    for(c=0; c<4; c++) {  // Para cada secuencia de colores del test...
      for(p=0; p<50; p++) { // para cada píxel...
        for(i=0; i<3; i++) {   // para cada R,G,B...
          RGB[i] = testColor[testOffset[c] + i];
        }
      strip.setPixelColor(p,Color(RGB[2],RGB[1],RGB[0]));
     }
     strip.show();
     if(c < 3) delay(1000);      
    }    
    Serial.print("Ada\n");                 // Envio de la cadena ACK al host
    lastByteTime = lastAckTime = millis(); // Se inicializan los contadores de tiempo   
}
 
//-----------------------------------------------------------//
 
 
//---------------------------COLOR---------------------------//
 
unsigned int Color(byte r, byte g, byte b){
  //Take the lowest 5 bits of each value and append them end to end
  return( ((unsigned int)g & 0x1F )<<10 | ((unsigned int)b & 0x1F)<<5 | (unsigned int)r & 0x1F);
}
 
//-----------------------------------------------------------//
 
 
//---------------------------TIMEOUT-------------------------//
 
// Esta función se llama cuando no hay datos serie pendientes disponibles
static boolean timeout(
  unsigned long t,       // Tiempo actual, milisegundos
  int           nLEDs) { // Numero de LEDs
 
  // Si la condición persiste enviar un ACK al host cada segundo para alertar de nuestra presencia.
  if((t - lastAckTime) > 1000) {
    Serial.print("Ada\n"); // Envio de la cadena ACK al host
    lastAckTime = t;       // Resetea contadores
  }
 
  // Si no se reciben datos durante un determinado periodo de tiempo se apagan los LEDS.
  if((t - lastByteTime) > serialTimeout) {
    for (int p=0; p<=nLEDs; p++){
      for (int i=0; i<3; i++){
        RGB[i]=0x80;
      }
      strip.setPixelColor(p,Color(RGB[0],RGB[1],RGB[2]));
    }
    strip.show();      // Enviamos los datos a los LEDs
    lastByteTime  = t; // Reseteo de contadores
    bytesBuffered = 0; // Vaciado del buffer serie
    return true;
  }
 
  return false; // No timeout
}
 
//-----------------------------------------------------------//
 
 
static const uint8_t byteOrder[] = { 1, 0, 2 };  //Esto especifica el orden de los bytes 0-R  1-G  2-B
 
 
//---------------------------LOOP----------------------------//
 
void loop(){
  
  uint8_t       i, hi, lo, byteNum;
  int           c,p;
  long          nLEDs, remaining;
  unsigned long t;
 
  // HEADER-SEEKING BLOCK: localiza la 'palabra mágica' al inicio del cuadro.
 
  // Si existe algún dato en el buffer se mueve hasta la posición de inicio.
  for(i=0; i<bytesBuffered; i++)
    buffer[i] = buffer[HEADERSIZE - bytesBuffered + i];
 
  // Se leen bytes del buffer serial hasta tener un encabezado completo.
  while(bytesBuffered < HEADERSIZE) {
    t = millis();
    if((c = Serial.read()) >= 0) {    // Datos recibidos?
      buffer[bytesBuffered++] = c;    // Almacenar en buffer
      lastByteTime = lastAckTime = t; // Reseteo de contadores timeout
    } else {                          // Sin datos, comprobar timeout...
      if(timeout(t, 10000) == true) return; // Volver a empezar
    }
  }
 
  // Tenemos un encabezado.  Comprobar coincidencia de la 'palabra mágica'
  for(i=0; i<MAGICSIZE; i++) {
    if(buffer[i] != magic[i]) {      // No hay coincidencia...
      if(i == 0) bytesBuffered -= 1; // Continuar la búsqueda en el siguiente caracter
      else       bytesBuffered -= i; // Continuar la búsqueda en un caracter no coincidente
      return;
    }
  }
 
  // La 'palabra mágica' coincide. Comprobemos el checksum.
  hi = buffer[MAGICSIZE];
  lo = buffer[MAGICSIZE + 1];
  if(buffer[MAGICSIZE + 2] != (hi ^ lo ^ 0x55)) {
    bytesBuffered -= MAGICSIZE; // No coincide, continuar despues de la 'palabra mágica'
    return;
  }
 
  // Checksum parece válido.  Obtener el conteo de LEDs de 16-bit LED, añadir 1( nLEDs SIEMPRE> 0)
  nLEDs = remaining = 256L * (long)hi + (long)lo + 1L;
  bytesBuffered = 0; // Limpiar el buffer serial
  byteNum = 0;
  
   // DATA-FORWARDING BLOCK: mueve los bytes de serial input a output.
 
  // Desafortunadamente no se pueden enviar los bytes directamente. El orden de los datos
  // es diferente en LPD6803 (R,B,G), además los bytes han de enviarse en grupos de tres
  // y para todos los LEDs a la vez
  
  p=0;
  while(remaining > 0) { // Mientras se esperen datos de LEDS
    t = millis();
    if((c = Serial.read()) >= 0) {    // Lectura satisfactoria?
      lastByteTime = lastAckTime = t; // Reseto contadores timeout 
      buffer[byteNum++] = c;          // Almaceno los datos en el buffer
      if(byteNum == 3) {              // Tengo todos los datos de un LED?
        while(byteNum > 0) {          // Los ponemos en el orden adecuado
            for (i=0; i<3; i++){
              RGB[i]= 0x80 | (buffer[byteOrder[--byteNum]] >> 1);
            }
        }
        remaining--;
        strip.setPixelColor(p,Color(RGB[0],RGB[1],RGB[2])); //Definimos el color del LED
        p=p++;
      }
    } else { // No hay datos, comprobar timeout...
      if(timeout(t, nLEDs) == true) return; // Volvera a empezar
    }
  }
 
  // Final de los datos. Final esperado
  strip.show();  //y los mostramos 
 
}
 
//-----------------------------------------------------------//
