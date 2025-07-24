/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : LucasRoide
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "queue.h"
#include "semphr.h"
#include <string.h>
#include <stdlib.h>
#include "stdio.h"
#include "atraso.h"
#include "defPrincipais.h"
#include "st7735.h"
#include "PRNG_LFSR.h"
#include "st7735.h"


/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define NR_ITENS_DISPLAY 8
#define NR_CAMADAS_DISPLAY 3
#define NR_MAX_ASTEROIDE 4
#define NR_TASKS_USANDO_TEMPO 3
#define PASSO_DE_TEMPO 20                                  // em millissegundos
#define NR_MAX_ITENS_COLIDIDOS  16                        // fazer par
#define MENSAGEM_ASTEROIDE_EXPLOSAO 255
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

SPI_HandleTypeDef hspi1;

TaskHandle_t tarefa_placar_vida;
TaskHandle_t tarefa_display;
TaskHandle_t tarefa_mov_nave;
TaskHandle_t tarefa_mov_aste;
TaskHandle_t tarefa_mov_tiro;
TaskHandle_t tarefa_contar_tempo;
TaskHandle_t tarefa_colisao;

QueueHandle_t fila_tempo;
QueueHandle_t fila_movimento;
QueueHandle_t fila_movimento_deletar;
QueueHandle_t fila_colisao;
QueueHandle_t fila_obj_Nave;
QueueHandle_t fila_obj_Tiro;
QueueHandle_t fila_obj_Asteroide;
QueueHandle_t fila_asteroide_atingido;
QueueHandle_t fila_tiro_antigiu;
QueueHandle_t fila_pontuacao;

SemaphoreHandle_t semaforo_nave_explode;
SemaphoreHandle_t semaforo_nave_bate;
SemaphoreHandle_t semaforo_nave_mudou_sprite;
SemaphoreHandle_t semaforo_jogador_ganhou_ponto;
SemaphoreHandle_t semaforo_uso_display;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128,
  .priority = (osPriority_t) osPriorityNormal,
};

/* USER CODE BEGIN PV */

/*
 * todas as structs usadas
 */

typedef struct {
	FigDef figura;
	int16_t posicao[2];
	int16_t pos_anterior[2];
	uint32_t cor;
	uint8_t camada;
	uint8_t id;

} display;


// enum flags fila display texto e numero
enum {
	texto,
	numero,
	texto_e_numero
};

// enum de loop de jogo
enum {
	Primeiro_Estado,
	Segundo_Estado,
	Terceiro_Estado
};

// enum de mensagens do jogo
enum {
	Nave_obj,
	Asteroide_1_obj,
	Asteroide_2_obj,
	Asteroide_3_obj,
	Asteroide_4_obj,
	Tiro_vertical_obj,
	Tiro_horizontal_obj,
	Vida_Nave_texto
};

// enum tiro posições (mudar isso porque foi uma péssima ideia fazer desse jeito!!!!)
enum {
	horizontal_positivo,
	horizontal_negativo,
	vertical_positivo,
	vertical_negativo
};

//enum basico
enum {
	falso,
	verdadeiro
};

typedef struct {
	uint8_t vida_nave;
	display Nave_display;
	uint8_t direcao;
} Nave_Struct;

// Tiro implementado com coordenadas (futuramente ajeitar, não foi melhor escolha)
typedef struct {
	uint8_t dano;
	display Tiro_display[2];  				// tiro horizontal e vertical
	uint8_t tiro_tela[2];     				// [0] -> horizontal, [1] -> vertical  | 0 ou 1

	// [0] -> horizontal, [1] -> vertical  | enum horizontal_positivo, horizontal_negativo, vertical_positivo, vertical_negativo
	uint8_t tiro_tela_direcao[2];
	// problemas ao apagar tiro da tela, não faço ideia de como resolvi, mas ta funcionando... (o problema era que não apaga na borda superior quando,
	// trocava o sentido da nave)
} Tiro_Struct;


typedef struct {
	uint8_t vida;
	uint8_t dano;
	uint8_t habilitado;
	int8_t  velocidade[2];
	display Asteroide_display;
} Asteroide_Struct;


// display zerado, usado quando se quer deletar alguma imagem da tela
display zero_tela_; // jeito de arrumar aquele problema de ficar bala no canto da tela!!!

Asteroide_Struct Asteroides[NR_MAX_ASTEROIDE];
Nave_Struct Nave;

uint16_t ADC_buffer[2];
uint16_t valor_ADC[2];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SPI1_Init(void);
static void MX_ADC1_Init(void);
void StartDefaultTask(void *argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// Função para pegar valor do buffer do joystick
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc){
	    valor_ADC[0] = ADC_buffer[0];
		valor_ADC[1] = ADC_buffer[1];
}

// funções principais das etapas do jogo.
void TelaInicial();
uint16_t Jogar();
void Tela_de_Morte(uint16_t);


// funções auxiliares
void vTask_Placar_Vida();
void vTask_Contar_Tempo();
void Asteroide_Construtor_Basico(Asteroide_Struct *, uint8_t);



void vTask_Contar_Tempo() // Contador de tempo para controle do jogo
{
	while(1)
	{
		for (uint8_t nr_tasks = 0; nr_tasks < NR_TASKS_USANDO_TEMPO; nr_tasks++){
			xQueueSendToBack(fila_tempo,&nr_tasks,0);
		};
		vTaskDelay(PASSO_DE_TEMPO);
	}
}


void vTask_Display(void *pvParameters)
{
	display figura_em_display;
	uint8_t figura_em_display_deletar;

	BaseType_t status_imprimir_objetos;
	BaseType_t status_deletar_objetos;

	int figuras_em_tela[NR_ITENS_DISPLAY] = {0,0,0,0,0,0};
	display figuras_em_tela_display[NR_ITENS_DISPLAY] = {0};

	int16_t pos_figura_x;
	int16_t largura_figura;
	int16_t tamanho_figura_x;

	int16_t pos_figura_y;
	int16_t altura_figura;
	int16_t tamanho_figura_y;

	portTickType xLastWakeTime;
	xLastWakeTime = xTaskGetTickCount();

	while(1)
	{
		xSemaphoreTake(semaforo_uso_display,portMAX_DELAY);  // <- gambiarra para não colocar outra estrutura só para texto, "depois" arrumo
		status_imprimir_objetos = xQueueReceive(fila_movimento,&figura_em_display,0);
		status_deletar_objetos = xQueueReceive(fila_movimento_deletar,&figura_em_display_deletar,0);

		while (status_imprimir_objetos == pdPASS)
		{
			if(figuras_em_tela[figura_em_display.id] == 0)
			{
				figuras_em_tela[figura_em_display.id] = 1;
				figuras_em_tela_display[figura_em_display.id] = figura_em_display;
			}



			//apagar a anterior
			if(figura_em_display.posicao[0] < 0)
			{
				figura_em_display.posicao[0] = 0;
			}
			if(figura_em_display.posicao[1] < 0)
			{
				figura_em_display.posicao[1] = 0;
			}

			if(figura_em_display.pos_anterior[0] < 0)
			{
				figura_em_display.pos_anterior[0] = 0;
			}
			if(figura_em_display.pos_anterior[1] < 0)
			{
				figura_em_display.pos_anterior[1] = 0;
			}

			ST7735_FillRectangle((uint16_t)figura_em_display.pos_anterior[0], (uint16_t)figura_em_display.pos_anterior[1], (uint16_t)figura_em_display.figura.width, (uint16_t)figura_em_display.figura.height, ST7735_BLACK);

			ST7735_draw_figure((uint32_t)figura_em_display.posicao[0], (uint32_t)figura_em_display.posicao[1], figura_em_display.figura, figura_em_display.cor);

			// atualiza a antiga imagem do diplay
			figuras_em_tela_display[figura_em_display.id] = figura_em_display;


			pos_figura_x = figura_em_display.posicao[0];
			largura_figura = (int16_t)figura_em_display.figura.width;
			tamanho_figura_x = (pos_figura_x + largura_figura);

			pos_figura_y = figura_em_display.posicao[1];
			altura_figura = (int16_t)figura_em_display.figura.height;
			tamanho_figura_y = (pos_figura_y + altura_figura);

			for (int camada = 0; camada < NR_CAMADAS_DISPLAY; camada++){
				for (int j = 0; j < NR_ITENS_DISPLAY; j++){
					if(figuras_em_tela[j] == verdadeiro){
						if (figuras_em_tela_display[j].id == figura_em_display.id){
						} else{
							// checa colisão no eixo x INICIO
							uint8_t a = ( pos_figura_x - figuras_em_tela_display[j].posicao[0]) <= (figuras_em_tela_display[j].figura.width);
							uint8_t b = (figuras_em_tela_display[j].posicao[0]) <= (tamanho_figura_x);
							uint8_t c = 0;

							//checa colisão no eixo y INICIO
							uint8_t d = (pos_figura_y - figuras_em_tela_display[j].posicao[1]) <= (figuras_em_tela_display[j].figura.height);
							uint8_t e = (figuras_em_tela_display[j].posicao[1]) <= (tamanho_figura_y);
							uint8_t f = 0;
							if ((pos_figura_x - figuras_em_tela_display[j].posicao[0]) > 0){
								c = a;
							} else{
								c = b;
							}
							// checa colisão no eixo x FIM

							if ((pos_figura_y - figuras_em_tela_display[j].posicao[1]) > 0){
								f = d;
							} else{
								f = e;
							}
							// checa colisão no eixo y FIM
							if (c && f){ // se tem colisão entre figuras
								if(figuras_em_tela_display[j].camada == camada){
									xQueueSendToBack(fila_colisao,&(figura_em_display.id),0);
									xQueueSendToBack(fila_colisao,&(figuras_em_tela_display[j].id),0);
									ST7735_draw_figure((uint32_t)figuras_em_tela_display[j].posicao[0], (uint32_t)figuras_em_tela_display[j].posicao[1], figuras_em_tela_display[j].figura, figuras_em_tela_display[j].cor);
								}
							}
						}
					}
				}
			}
			status_imprimir_objetos = xQueueReceive(fila_movimento,&figura_em_display,0);
		}

		while (status_deletar_objetos == pdPASS){
					ST7735_FillRectangle((uint16_t)figuras_em_tela_display[figura_em_display_deletar].posicao[0], (uint16_t)figuras_em_tela_display[figura_em_display_deletar].posicao[1], (uint16_t)figuras_em_tela_display[figura_em_display_deletar].figura.width, (uint16_t)figuras_em_tela_display[figura_em_display_deletar].figura.height, ST7735_BLACK);
					figuras_em_tela[figura_em_display_deletar] = 0;
					figuras_em_tela_display[figura_em_display_deletar] = zero_tela_;
					status_deletar_objetos = xQueueReceive(fila_movimento_deletar,&figura_em_display_deletar,0);
		}
		xSemaphoreGive(semaforo_uso_display);
		vTaskDelayUntil( &xLastWakeTime,(8 / portTICK_RATE_MS));
	}
}

void vTask_Colisao(void *pvParameters){
	uint8_t obj_id_1;
	uint8_t obj_id_2;
	uint8_t mensagem_explosao = MENSAGEM_ASTEROIDE_EXPLOSAO;
	uint8_t contar_colisao = verdadeiro;
	uint8_t conta_tempo_nova_colisao_asteroide_nave = 0;
	uint8_t nr_tamanho_lista_colisao=0;

	while(xQueueReceive(fila_colisao,&obj_id_1,0) == pdPASS){

	}

	while(1){
		xQueueReceive(fila_colisao,&obj_id_1,portMAX_DELAY);
		xQueueReceive(fila_colisao,&obj_id_2,portMAX_DELAY);

		if(((obj_id_1 == Nave_obj)||(obj_id_2 == Nave_obj))&&(((obj_id_1 >= Asteroide_1_obj)&&(obj_id_1 <= Asteroide_4_obj))||((obj_id_2 >= Asteroide_1_obj)&&(obj_id_2 <= Asteroide_4_obj)))&&(contar_colisao)){
			if((obj_id_1 >= Asteroide_1_obj)&&(obj_id_1 <= Asteroide_4_obj)){
				xQueueSendToBack(fila_asteroide_atingido,&mensagem_explosao,0);
				xQueueSendToBack(fila_asteroide_atingido,&obj_id_1,0);
			}
			else if((obj_id_2 >= Asteroide_1_obj)&&(obj_id_2 <= Asteroide_4_obj)){
				xQueueSendToBack(fila_asteroide_atingido,&mensagem_explosao,0);
				xQueueSendToBack(fila_asteroide_atingido,&obj_id_2,0);
			}
			xSemaphoreGive(semaforo_nave_bate);
			contar_colisao = falso;
			while(nr_tamanho_lista_colisao < NR_MAX_ITENS_COLIDIDOS){
				xQueueReceive(fila_colisao,&obj_id_1,0);
				xQueueReceive(fila_colisao,&obj_id_2,0);
				if(((obj_id_1 == Nave_obj)||(obj_id_2 == Nave_obj))&&(((obj_id_1 >= Asteroide_1_obj)&&(obj_id_1 <= Asteroide_4_obj))||((obj_id_2 >= Asteroide_1_obj)&&(obj_id_2 <= Asteroide_4_obj)))&&(contar_colisao)){

				}
				else{
					xQueueSendToBack(fila_colisao,&obj_id_1,0);
					xQueueSendToBack(fila_colisao,&obj_id_2,0);
				}
				nr_tamanho_lista_colisao++;
			}
			nr_tamanho_lista_colisao = 0;
		}

		if(((obj_id_2 == Tiro_vertical_obj)|(obj_id_1 == Tiro_horizontal_obj)||(obj_id_1 == Tiro_vertical_obj)||(obj_id_2 == Tiro_horizontal_obj))&&(((obj_id_1 >= Asteroide_1_obj)&&(obj_id_1 <= Asteroide_4_obj))||((obj_id_2 >= Asteroide_1_obj)&&(obj_id_2 <= Asteroide_4_obj)))){
			xSemaphoreGive(semaforo_jogador_ganhou_ponto);
			if((obj_id_2 == Tiro_vertical_obj)||(obj_id_2 == Tiro_horizontal_obj)){
				xQueueSendToBack(fila_tiro_antigiu,&obj_id_2,0);
			} else{
				xQueueSendToBack(fila_asteroide_atingido,&obj_id_2,0);
			}
			if((obj_id_1 == Tiro_vertical_obj)||(obj_id_1 == Tiro_horizontal_obj)){
				xQueueSendToBack(fila_tiro_antigiu,&obj_id_1,0);
			} else{
				xQueueSendToBack(fila_asteroide_atingido,&obj_id_1,0);
			}
			while(nr_tamanho_lista_colisao < NR_MAX_ITENS_COLIDIDOS){ //teste inicio
				xQueueReceive(fila_colisao,&obj_id_1,0);
				xQueueReceive(fila_colisao,&obj_id_2,0);
				if(((obj_id_2 == Tiro_vertical_obj)|(obj_id_1 == Tiro_horizontal_obj)||(obj_id_1 == Tiro_vertical_obj)||(obj_id_2 == Tiro_horizontal_obj))&&(((obj_id_1 >= 1)&&(obj_id_1 <= 4))||((obj_id_2 >= 1)&&(obj_id_2 <= 4)))&&(contar_colisao)){

				}
				else{
					xQueueSendToBack(fila_colisao,&obj_id_1,0);
					xQueueSendToBack(fila_colisao,&obj_id_2,0);
				}
				nr_tamanho_lista_colisao++;
			}
			nr_tamanho_lista_colisao = 0;
		} //teste fim


		// cronômetro para a próxima ação de colisão
		xQueueReceive(fila_tempo,&obj_id_1,portMAX_DELAY);
		conta_tempo_nova_colisao_asteroide_nave++;
		if(conta_tempo_nova_colisao_asteroide_nave == 30){
			conta_tempo_nova_colisao_asteroide_nave = 0;
			contar_colisao = verdadeiro;
		}
	}

}

void vTask_Nave_Mover(void *pvParameters)
{
	uint8_t x = Nave.Nave_display.posicao[0];
	uint8_t y = Nave.Nave_display.posicao[1];
	uint32_t dif_eixoX;
	uint32_t dif_eixoY;
	uint8_t enviar = 0;

	xQueueSendToBack(fila_movimento, &(Nave.Nave_display), portMAX_DELAY);

	while(1)
	{

		dif_eixoX = (1<<12) - valor_ADC[1];// media_eixoX;
		dif_eixoY = (1<<12) - valor_ADC[0];//media_eixoY;

		Nave.Nave_display.pos_anterior[0] = x;
		Nave.Nave_display.pos_anterior[1] = y;

		if(dif_eixoX < 1800)   // aprox. 200 de segurança
		{
			if(x>=0)
			{
				x--;
				Nave.Nave_display.figura = tela_nave_180;
				Nave.direcao = horizontal_negativo;
			}
			if (x<=0)
			{
				x = 150;
			}
			enviar = 1;
		}
		else if(dif_eixoX > 3800)
		{
			if(x<=150)
			{
				x++;
				Nave.Nave_display.figura  = tela_nave_0;
				Nave.direcao = horizontal_positivo;
			}
			if(x>=151)
			{
				x = 0;
			}
			enviar = 1;
		}
		if(dif_eixoY < 1800)
		{
			if(y>0)
			{
				y--;
				Nave.Nave_display.figura  = tela_nave_90;
				Nave.direcao = vertical_negativo;
			}
			if (y<=0)
			{
				y = 119;
			}
			enviar = 1;
		}
		else if(dif_eixoY > 3800)
		{
			if(y<=121)
			{
				y++;
				Nave.Nave_display.figura  = tela_nave_270;
				Nave.direcao = vertical_positivo;
			}
			if(y>=120)
			{
				y = 0;
			}
			enviar = 1;
		}

		Nave.Nave_display.posicao[0] = x;
		Nave.Nave_display.posicao[1] = y;

		if(enviar)
		{
			xQueueSendToBack(fila_movimento, &(Nave.Nave_display), portMAX_DELAY);
		}
		enviar = 0;

		vTaskDelay(20);
	}
}


void vTask_Asteroide_Mover(void *pvParameters)
{
	uint16_t nr_aleatorio = 0;
	BaseType_t status_fila_tempo;
	BaseType_t status_fila_atingido;
	uint16_t tempo = 0;
	uint8_t id_asteroide;

	//Verificar colisao
	uint8_t asteroide_colidido;

	// valores INICIO
	uint8_t posicao_x;
	uint8_t posicao_y;
	int8_t velocidade_x;
	int8_t velocidade_y;
	FigDef figura;


	//importante, cópia dos asteroides para muda-los de posição.
	Asteroide_Struct asteroide_vtask[NR_MAX_ASTEROIDE];

	xQueueReceive(fila_obj_Asteroide, &asteroide_vtask, portMAX_DELAY);

	xQueueSendToBack(fila_movimento, &(asteroide_vtask[0]), portMAX_DELAY);
	xQueueSendToBack(fila_movimento, &(asteroide_vtask[1]), portMAX_DELAY);
	xQueueSendToBack(fila_movimento, &(asteroide_vtask[2]), portMAX_DELAY);
	xQueueSendToBack(fila_movimento, &(asteroide_vtask[3]), portMAX_DELAY);

	while(1)
	{
		status_fila_atingido = xQueueReceive(fila_asteroide_atingido,&asteroide_colidido,0);
		if(status_fila_atingido == pdPASS){
			if(asteroide_colidido == MENSAGEM_ASTEROIDE_EXPLOSAO){ //debug
				xQueueReceive(fila_asteroide_atingido,&asteroide_colidido,portMAX_DELAY);
				asteroide_vtask[asteroide_colidido - 1].vida = 1;
			}


			asteroide_vtask[asteroide_colidido - 1].vida = asteroide_vtask[asteroide_colidido - 1].vida - 1;
			if(asteroide_vtask[asteroide_colidido - 1].vida == 1<<8){
				asteroide_vtask[asteroide_colidido - 1].vida = 0;
			}
			if (asteroide_vtask[asteroide_colidido - 1].vida == 0){
				asteroide_vtask[asteroide_colidido - 1].habilitado = 0; // o -1 se deve porque o id dos asteroides começa depois do id da nave e no vetor de asteroides eles começam do 0
				id_asteroide = asteroide_vtask[asteroide_colidido - 1].Asteroide_display.id;
				xQueueSendToBack(fila_movimento_deletar,&(id_asteroide),portMAX_DELAY);
			}
		}

		status_fila_tempo = xQueueReceive(fila_tempo,&(nr_aleatorio),0); 	// recebe um tick de tempo
		if (status_fila_tempo == pdPASS){
			tempo++;
		}

		if(tempo == 10){ 													// a cada 20 segundos habilita um novo asteroide
			tempo = 0; 														// reset
			for (uint8_t i = 0; i< NR_MAX_ASTEROIDE; i++) {					// checa todos os asteroides

				if (asteroide_vtask[i].habilitado == 0) { // se nao tiver criado ainda
					Asteroide_Construtor_Basico(&(asteroide_vtask[i]),i+Asteroide_1_obj);
					asteroide_vtask[i].habilitado = 1;						// habilita-o

					// Valores da nave atual (ADC), para criar um novo asteroide.
					nr_aleatorio = valor_ADC[0]*10 - valor_ADC[1]*7;
					if(nr_aleatorio < 0){
						nr_aleatorio = (valor_ADC[1] - valor_ADC[0])*13;
					}

					/*
					 * valores aleatórios para criação de um novo asteroide.
					 *
					 * Problema!!! -> Caso a nave se manter parada, os asteroides vao sair do mesmo ponto!!! (Arrumar se quiser)
					 */
					switch(nr_aleatorio%4){
						case(horizontal_positivo):
							posicao_x = 159;
							posicao_y = nr_aleatorio%128;
							velocidade_x = nr_aleatorio%2 - 2;
							velocidade_y = nr_aleatorio%3 - 1;
							figura = tela_asteroide_1;
							break;
						case(horizontal_negativo):
							posicao_x = 2;
							posicao_y = nr_aleatorio%128;
							velocidade_x = nr_aleatorio%2;
							velocidade_y = nr_aleatorio%3 - 1;
							figura = tela_asteroide_2;
							break;
						case(vertical_positivo):
							posicao_x = nr_aleatorio%160;
							posicao_y = 124;
							velocidade_x = nr_aleatorio%3 - 1;
							velocidade_y = nr_aleatorio%2 - 2;
							figura = tela_asteroide_2;
							break;
						case(vertical_negativo):
							posicao_x = nr_aleatorio%160;
							posicao_y = 2;
							velocidade_x = nr_aleatorio%3 - 1;
							velocidade_y = nr_aleatorio%3;
							figura = tela_asteroide_4;
							break;
						default:
							posicao_x = 3;
							posicao_y = 120;
							velocidade_x = 1;
							velocidade_y = -2;
							figura = tela_asteroide_4;
							break;
					}
					// definindo valores do asteroide
					asteroide_vtask[i].Asteroide_display.figura = figura;
					asteroide_vtask[i].Asteroide_display.posicao[0] = posicao_x;
					asteroide_vtask[i].Asteroide_display.posicao[1] = posicao_y;
					asteroide_vtask[i].Asteroide_display.pos_anterior[0] = posicao_x;
					asteroide_vtask[i].Asteroide_display.pos_anterior[0] = posicao_y;
					asteroide_vtask[i].velocidade[0] = velocidade_x;
					asteroide_vtask[i].velocidade[1] = velocidade_y;
					break;
				}
			}
		}

		// parte de movimentar os asteroides

		for(uint8_t i = 0; i< NR_MAX_ASTEROIDE; i++)
		{

			if(asteroide_vtask[i].habilitado)
			{
				asteroide_vtask[i].Asteroide_display.pos_anterior[0] = asteroide_vtask[i].Asteroide_display.posicao[0];
				asteroide_vtask[i].Asteroide_display.pos_anterior[1] = asteroide_vtask[i].Asteroide_display.posicao[1];
				asteroide_vtask[i].Asteroide_display.posicao[0] = asteroide_vtask[i].Asteroide_display.posicao[0] + asteroide_vtask[i].velocidade[0];
				asteroide_vtask[i].Asteroide_display.posicao[1] = asteroide_vtask[i].Asteroide_display.posicao[1] + asteroide_vtask[i].velocidade[1];

				if ((asteroide_vtask[i].Asteroide_display.posicao[0]  >= 148) || (asteroide_vtask[i].Asteroide_display.posicao[0]  <= 2)|| (asteroide_vtask[i].Asteroide_display.posicao[1] >= 124)||(asteroide_vtask[i].Asteroide_display.posicao[1] <= 2))
				{
					asteroide_vtask[i].habilitado = 0;
					id_asteroide = i + Asteroide_1_obj;
					xQueueSendToBack(fila_movimento_deletar,&(id_asteroide),portMAX_DELAY);
				}
				else
				{
					xQueueSendToBack(fila_movimento,&(asteroide_vtask[i].Asteroide_display),portMAX_DELAY);
				}
			}
		}
		vTaskDelay(250);
	}
}

// gera os asteroides e qual a figura deles

void Asteroide_Construtor_Basico(Asteroide_Struct *asteroide, uint8_t obj_id)
{
	switch (obj_id) // tem que ser os mesmos ids anteriormente definidos
	{
		case (Asteroide_1_obj):
			asteroide->Asteroide_display.figura = tela_asteroide_1;
			asteroide->Asteroide_display.id = Asteroide_1_obj;
			//asteroide->Asteroide_display.cor = ST7735_CYAN;
			break;
		case (Asteroide_2_obj):
			asteroide->Asteroide_display.figura = tela_asteroide_2;
			asteroide->Asteroide_display.id = Asteroide_2_obj;
			//asteroide->Asteroide_display.cor = ST7735_BLUE;
			break;
		case (Asteroide_4_obj):
			asteroide->Asteroide_display.figura = tela_asteroide_4;
			asteroide->Asteroide_display.id = Asteroide_4_obj;
			//asteroide->Asteroide_display.cor = ST7735_WHITE;
			break;
	}

	asteroide->Asteroide_display.cor = ST7735_WHITE;
	asteroide->Asteroide_display.camada = 0;
	asteroide->Asteroide_display.pos_anterior[0] = 0;
	asteroide->Asteroide_display.pos_anterior[1] = 0;
	asteroide->Asteroide_display.posicao[0] = 0;
	asteroide->Asteroide_display.posicao[1] = 0;
	asteroide->velocidade[0] = 2;
	asteroide->velocidade[1] = 2;
	asteroide->dano = 2;
	asteroide->habilitado = 0;
	asteroide->vida = 2;
}



/*
 * Task de tiro, tava dando problema, mas adicionei uma fila de tempo e vTaskDelay(14), parou de ficar tiro na tela (Não da de entender!!!!)
 */
void vTask_Tiro(void *pvParameter) {
	Tiro_Struct Tiro;

	xQueueReceive(fila_obj_Tiro, &Tiro, portMAX_DELAY);

	uint8_t obj_tiro; // id para tiro excluido da tela
	BaseType_t status_fila_tiro_atingiu;

	// caracteristicas do tiro
	uint8_t tempo_tiro_x = 0;
	uint8_t tempo_tiro_y = 0;
	uint8_t tiro_tela_x = 0;
	uint8_t tiro_tela_y = 0;
	uint8_t direcao_tiro;

	BaseType_t status_fila_tempo;

	uint8_t x_tiro_horizontal = Tiro.Tiro_display[0].posicao[0];
	uint8_t y_tiro_horizontal = Tiro.Tiro_display[0].posicao[1];

	uint8_t x_tiro_vertical = Tiro.Tiro_display[1].posicao[0];
	uint8_t y_tiro_vertical = Tiro.Tiro_display[1].posicao[1];

	xQueueSendToBack(fila_movimento, &(Tiro.Tiro_display[0]), portMAX_DELAY);
	xQueueSendToBack(fila_movimento, &(Tiro.Tiro_display[1]), portMAX_DELAY);

	while (1) {
		status_fila_tiro_atingiu = xQueueReceive(fila_tiro_antigiu, &obj_tiro, 0);
		if (status_fila_tiro_atingiu == pdPASS) {
			if (obj_tiro == Tiro_horizontal_obj) {
				Tiro.tiro_tela[0] = 0;
			} else {
				Tiro.tiro_tela[1] = 0;
			}
			xQueueSendToBack(fila_movimento_deletar, &(obj_tiro), portMAX_DELAY);
		}

		tiro_tela_x = Tiro.tiro_tela[0];
		tiro_tela_y = Tiro.tiro_tela[1];

		if (tiro_tela_x || tiro_tela_y) {
			if (tiro_tela_x) {
				switch (Tiro.tiro_tela_direcao[0]) {
					case (horizontal_positivo): {
						Tiro.Tiro_display[0].pos_anterior[0] = x_tiro_horizontal;
						x_tiro_horizontal = x_tiro_horizontal + 1;
						Tiro.Tiro_display[0].posicao[0] = x_tiro_horizontal;
						if (x_tiro_horizontal >= 159) {
							Tiro.tiro_tela[0] = 0;
						} else {
							xQueueSendToBack(fila_movimento, &(Tiro.Tiro_display[0]), portMAX_DELAY);
						}
						break;
					}
					case (horizontal_negativo): {
						Tiro.Tiro_display[0].pos_anterior[0] = x_tiro_horizontal;
						x_tiro_horizontal = x_tiro_horizontal - 1;
						Tiro.Tiro_display[0].posicao[0] = x_tiro_horizontal;
						if (x_tiro_horizontal <= 0) {
							Tiro.tiro_tela[0] = 0;
							obj_tiro = Tiro.Tiro_display[0].id;
							xQueueSendToBack(fila_movimento_deletar, &(obj_tiro), portMAX_DELAY);
						} else {
							xQueueSendToBack(fila_movimento, &(Tiro.Tiro_display[0]), portMAX_DELAY);
						}
						break;
					}
				}
			}
			if (tiro_tela_y) {
				switch (Tiro.tiro_tela_direcao[1]) {
					case (vertical_positivo): {
						Tiro.Tiro_display[1].pos_anterior[1] = y_tiro_vertical;
						y_tiro_vertical = y_tiro_vertical + 1;
						Tiro.Tiro_display[1].posicao[1] = y_tiro_vertical;
						if (y_tiro_vertical == 160) {
							Tiro.tiro_tela[1] = 0;
						} else {
							xQueueSendToBack(fila_movimento, &(Tiro.Tiro_display[1]), portMAX_DELAY);
						}
						break;
					}
					case (vertical_negativo): {
						Tiro.Tiro_display[1].pos_anterior[1] = y_tiro_vertical;
						y_tiro_vertical = y_tiro_vertical - 1;
						Tiro.Tiro_display[1].posicao[1] = y_tiro_vertical;
						if (y_tiro_vertical == 0) {
							Tiro.tiro_tela[1] = 0;
							obj_tiro = Tiro.Tiro_display[1].id;
							xQueueSendToBack(fila_movimento_deletar, &(obj_tiro), portMAX_DELAY);
						} else {
							xQueueSendToBack(fila_movimento, &(Tiro.Tiro_display[1]), portMAX_DELAY);
						}
						break;
					}
				}
			}
		} else {
			status_fila_tempo = xQueueReceive(fila_tempo, &(x_tiro_horizontal), 0);  // recebe um tick de tempo
			if (status_fila_tempo == pdPASS) {
				tempo_tiro_x++;
				tempo_tiro_y++;
			}

			if (tempo_tiro_x == 20) {  // 200
				tempo_tiro_x = 0;
				direcao_tiro = Nave.direcao;
				switch (direcao_tiro) {
					case (horizontal_positivo): {
						Tiro.tiro_tela[0] = 1;
						Tiro.tiro_tela_direcao[0] = horizontal_positivo;
						x_tiro_horizontal = Nave.Nave_display.posicao[0] + 18;
						y_tiro_horizontal = Nave.Nave_display.posicao[1] + 5;
						Tiro.Tiro_display[0].posicao[1] = y_tiro_horizontal;
						Tiro.Tiro_display[0].pos_anterior[1] = y_tiro_horizontal;
						break;
					}
					case (horizontal_negativo): {
						Tiro.tiro_tela[0] = 1;
						Tiro.tiro_tela_direcao[0] = horizontal_negativo;
						x_tiro_horizontal = Nave.Nave_display.posicao[0] - 6;
						y_tiro_horizontal = Nave.Nave_display.posicao[1] + 5;
						Tiro.Tiro_display[0].posicao[1] = y_tiro_horizontal;
						Tiro.Tiro_display[0].pos_anterior[1] = y_tiro_horizontal;
						break;
					}
					case (vertical_positivo): {
						Tiro.tiro_tela[1] = 1;
						Tiro.tiro_tela_direcao[1] = vertical_positivo;
						x_tiro_vertical = Nave.Nave_display.posicao[0] + 5;
						y_tiro_vertical = Nave.Nave_display.posicao[1] + 18;
						Tiro.Tiro_display[1].posicao[0] = x_tiro_vertical;
						Tiro.Tiro_display[1].pos_anterior[0] = x_tiro_vertical;
						break;
					}
					case (vertical_negativo): {
						Tiro.tiro_tela[1] = 1;
						Tiro.tiro_tela_direcao[1] = vertical_negativo;
						x_tiro_vertical = Nave.Nave_display.posicao[0] + 5;
						y_tiro_vertical = Nave.Nave_display.posicao[1] - 9;
						Tiro.Tiro_display[1].posicao[0] = x_tiro_vertical;
						Tiro.Tiro_display[1].pos_anterior[0] = x_tiro_vertical;
						break;
					}
				}
			}
		}
		vTaskDelay(14);
	}
}



/*
 * Problema aqui.
 * só começa a contar pontuação e a vida quando a primeira colisão da nave acontece!! (Ajeitar)
 */
void vTask_Placar_Vida()
{
	BaseType_t status_semaforo_jogador_ganhou_ponto;
	BaseType_t status_semaforo_nave_bate;
	uint8_t pontuacao;
	uint8_t vivo;

	//ST7735_WriteString(0, 0,"Vida:", Font_7x10, ST7735_WHITE, ST7735_BLACK);
	//ST7735_write_nr(40, 0, Nave.vida_nave, Font_7x10, ST7735_WHITE, ST7735_BLACK);

	status_semaforo_nave_bate = xSemaphoreTake(semaforo_nave_bate,portMAX_DELAY);

	while(1)
	{
		Nave.vida_nave = 2;
		pontuacao = 0;
		vivo = verdadeiro;

		while(vivo)
		{
			ST7735_WriteString(0, 0,"Vida:", Font_7x10, ST7735_WHITE, ST7735_BLACK);
			ST7735_write_nr(40, 0, Nave.vida_nave, Font_7x10, ST7735_WHITE, ST7735_BLACK);
			//ST7735_WriteString(0, 25,"Placar:   ", Font_7x10, ST7735_WHITE, ST7735_BLACK);
			ST7735_write_nr(0, 150, pontuacao, Font_7x10, ST7735_WHITE, ST7735_BLACK);
			xSemaphoreTake(semaforo_uso_display,portMAX_DELAY);
			status_semaforo_jogador_ganhou_ponto = xSemaphoreTake(semaforo_jogador_ganhou_ponto,0);
			if(status_semaforo_jogador_ganhou_ponto == pdPASS)
			{
				pontuacao++;
			}

			// aqui, colocar um semáforo para uma função que ve uma colisão de nave e asteroide (Ajustada!!!)
			status_semaforo_nave_bate = xSemaphoreTake(semaforo_nave_bate,0);
			if (status_semaforo_nave_bate ==pdPASS)
			{
				Nave.vida_nave--;

			}
			//ST7735_WriteString(0, 0,"Vida: ", Font_7x10, ST7735_WHITE, ST7735_BLACK);
			//ST7735_write_nr(63, 0, Nave.vida_nave, Font_7x10, ST7735_WHITE, ST7735_BLACK);
			if (Nave.vida_nave  == 0)
			{
				//xQueueSendToFront(fila_mensagens,&mensagens_enviar,portMAX_DELAY);
				xQueueSendToBack(fila_pontuacao, &pontuacao,portMAX_DELAY);
				xSemaphoreGive(semaforo_nave_explode);
				vivo = falso;
			}
			xSemaphoreGive(semaforo_uso_display); //<- a fazer certo, por agora só gambiarra
			vTaskDelay(300);
		}
	}
}



/*
 * Aqui será desenvolvido a maquina de estados do jogo (tela inicial -> jogo -> Tela final)
 */


// Loop principal do jogo, aqui é controlado todos os 3 estados.
void vTask_Loop_Jogo(void *pvPrameters)
{
	uint8_t estado = Primeiro_Estado;
	uint8_t placar;

	vTaskSuspend(tarefa_placar_vida);
	vTaskSuspend(tarefa_display);
	vTaskSuspend(tarefa_mov_nave);

	while(1)
	{
		switch(estado)
		{
			case Primeiro_Estado:
				TelaInicial();
				estado = Segundo_Estado;
				break;
			case Segundo_Estado:
				placar = Jogar();
				estado = Terceiro_Estado;
				break;
			case Terceiro_Estado:
				Tela_de_Morte(placar);
				estado = Primeiro_Estado;
				  //while(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_15)){
					  //ST7735_FillScreen(ST7735_BLACK);
					//  estado = Primeiro_Estado;
				  //}

				//estado = Primeiro_Estado;
				break;
			default:
				ST7735_WriteString(48, 59,"Nao era pra passar aqui (BUG)", Font_7x10, ST7735_RED, ST7735_BLACK);
				vTaskDelay(portMAX_DELAY);
		}

	}
}



// Figura da tela inicial do jogo LucasRoide
void TelaInicial()
{
	ST7735_draw_figure(18, 9, tela_asteroide_1 , ST7735_WHITE); 				// 1
	ST7735_draw_figure(112, 9, tela_asteroide_4 , ST7735_WHITE);				// 2
	ST7735_draw_figure(0, 46, tela_inicial_figura2, ST7735_WHITE);				// meio
	ST7735_draw_figure(18, 88, tela_asteroide_4 , ST7735_WHITE);				// 4
	ST7735_draw_figure(112, 88, tela_asteroide_1 , ST7735_WHITE);				// 1

	vTaskDelay(5000);
}



uint16_t Jogar()
{
	uint16_t placar = 0;

	Nave.vida_nave = 2;

	Nave.Nave_display.camada = 1;
	Nave.Nave_display.cor = ST7735_WHITE;
	Nave.Nave_display.id = Nave_obj;

	Nave.Nave_display.figura = tela_nave_0;
	Nave.Nave_display.pos_anterior[0] = 0;
	Nave.Nave_display.pos_anterior[1] = 0;
	Nave.Nave_display.posicao[0] = (160 - Nave.Nave_display.figura.width)/2;
	Nave.Nave_display.posicao[1] = (128 - Nave.Nave_display.figura.height)/2;
	//Nave FIM

	//Tiro INICIO
	Tiro_Struct Tiro;

	Tiro.dano = 1;

	Tiro.tiro_tela[0] = 0;
	Tiro.tiro_tela[1] = 0;
	Tiro.tiro_tela_direcao[0] = 0;
	Tiro.tiro_tela_direcao[1] = 0;

	Tiro.Tiro_display[0].camada = 0;
	Tiro.Tiro_display[0].cor = ST7735_GREEN;
	Tiro.Tiro_display[0].figura = tela_tiro;
	Tiro.Tiro_display[0].id = Tiro_horizontal_obj;
	Tiro.Tiro_display[0].pos_anterior[0] = 0;
	Tiro.Tiro_display[0].pos_anterior[1] = 0;
	Tiro.Tiro_display[0].posicao[0] = 0;
	Tiro.Tiro_display[0].posicao[1] = 0;

	Tiro.Tiro_display[1].camada = 0;
	Tiro.Tiro_display[1].cor = ST7735_GREEN;
	Tiro.Tiro_display[1].figura = tela_tiro;
	Tiro.Tiro_display[1].id = Tiro_vertical_obj;
	Tiro.Tiro_display[1].pos_anterior[0] = 0;
	Tiro.Tiro_display[1].pos_anterior[1] = 0;
	Tiro.Tiro_display[1].posicao[0] = 0;
	Tiro.Tiro_display[1].posicao[1] = 0;

	xQueueSendToBack(fila_obj_Tiro,&Tiro,0);
	//Tiro FIM

	// Asteroide INICIO
	Asteroide_Struct Asteroide[NR_MAX_ASTEROIDE];

	Asteroide_Construtor_Basico(&(Asteroide[0]), Asteroide_1_obj);
	Asteroide_Construtor_Basico(&(Asteroide[1]), Asteroide_2_obj);
	Asteroide_Construtor_Basico(&(Asteroide[2]), Asteroide_3_obj);
	Asteroide_Construtor_Basico(&(Asteroide[3]), Asteroide_4_obj);

	xQueueSendToBack(fila_obj_Asteroide,&Asteroide,0);
	// Asteroide FIM


	ST7735_FillRectangle(18, 9, 130, 33, ST7735_BLACK);
	ST7735_FillRectangle(0, 46, 160, 33, ST7735_BLACK);
	ST7735_FillRectangle(18,88, 130, 33, ST7735_BLACK);

	vTaskResume(tarefa_mov_nave);
	vTaskResume(tarefa_placar_vida);
	vTaskResume(tarefa_display);

	xQueueReceive(fila_pontuacao,&placar,portMAX_DELAY);
	xSemaphoreTake(semaforo_nave_explode,portMAX_DELAY);

	vTaskSuspend(tarefa_display);
	vTaskSuspend(tarefa_placar_vida);
	vTaskSuspend(tarefa_mov_nave);


	vTaskDelay(500);

	return placar;
}

void Tela_de_Morte(uint16_t placar)
{
	ST7735_FillScreen(ST7735_BLACK);
	ST7735_FillRectangle(0, 45, 160, 14, ST7735_BLACK);
	ST7735_draw_horizontal_line(0,160,45,ST7735_WHITE);
	vTaskDelay(500);
	ST7735_WriteString(52, 48,"GAME OVER", Font_7x10, ST7735_WHITE, ST7735_BLACK);
	vTaskDelay(500);
	ST7735_draw_horizontal_line(0,160,59,ST7735_WHITE);
	ST7735_WriteString(12, 25,"Placar:   ", Font_7x10, ST7735_WHITE, ST7735_BLACK);
	ST7735_write_nr(68, 25, placar, Font_7x10, ST7735_WHITE, ST7735_BLACK);
	ST7735_draw_figure(0, 65, tela_inicial_figura2, ST7735_WHITE);

	vTaskDelay(500);
	ST7735_WriteString(15, 110,"Press to Play Again", Font_7x10, ST7735_WHITE, ST7735_BLACK);

	while(HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_15))
	{

	};
	ST7735_FillScreen(ST7735_BLACK);
}
//---------------------------------------------------------------------------------------------------



/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI1_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */

	// inicializa LCD

  HAL_Delay(100);
  ST7735_Init();

  ST7735_FillScreen(ST7735_BLACK);

  // --------------------------------------------------------------------------------------

   HAL_ADC_Start_DMA(&hadc1,(uint32_t*)ADC_buffer,2);

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  vSemaphoreCreateBinary(semaforo_nave_explode);
  xSemaphoreTake(semaforo_nave_explode,0);

  vSemaphoreCreateBinary(semaforo_nave_bate);
  xSemaphoreTake(semaforo_nave_bate,0);

  vSemaphoreCreateBinary(semaforo_nave_mudou_sprite);
  xSemaphoreTake(semaforo_nave_mudou_sprite, 0);

  vSemaphoreCreateBinary(semaforo_jogador_ganhou_ponto);
  xSemaphoreTake(semaforo_jogador_ganhou_ponto,0);

  vSemaphoreCreateBinary(semaforo_uso_display);

  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  fila_tempo = xQueueCreate(NR_TASKS_USANDO_TEMPO, sizeof(uint8_t)); //tempo só funciona para funçoes round-and-robin
  fila_movimento = xQueueCreate(8, sizeof(display));
  fila_movimento_deletar = xQueueCreate(8,sizeof(uint8_t)); // id do item deletado
  //fila_movimento_texto_numero = xQueueCreate(1,sizeof(display_txt_nr)); // <-- aqui <- a fazer certo, por agora só gambiarra
  fila_colisao = xQueueCreate(NR_MAX_ITENS_COLIDIDOS,sizeof(uint8_t));
  fila_obj_Tiro = xQueueCreate(1,sizeof(Tiro_Struct));
  fila_obj_Asteroide = xQueueCreate(1,sizeof(Asteroide_Struct)*NR_MAX_ASTEROIDE);
  fila_asteroide_atingido = xQueueCreate(2,sizeof(uint8_t));
  fila_tiro_antigiu = xQueueCreate(1,sizeof(uint8_t));
  fila_pontuacao = xQueueCreate(1,sizeof(uint8_t));

  // filas objetos ...
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */


  BaseType_t status_placar_vida;
  BaseType_t status_display;
  BaseType_t status_loop_jogo;
  BaseType_t status_movimento_nave;
  BaseType_t status_movimento_tiro;
  BaseType_t status_movimento_asteroide;
  BaseType_t status_contar_tempo;
  BaseType_t status_colisao;

  	//-----------------------------------------------------------------------------------------//
  	//                              Tarefas Asteroides
    //-----------------------------------------------------------------------------------------//

  // unica task que tem maior prioridade, para printar tudo na tela. (se deixar prioridade igual da problema!!!)
	status_display = xTaskCreate(vTask_Display, "Display com Fila", 1024, NULL, osPriorityAboveNormal2, &tarefa_display);

	status_loop_jogo = xTaskCreate(vTask_Loop_Jogo, "Loop_de_jogo", 1024, NULL, osPriorityAboveNormal1, NULL);

    status_placar_vida = xTaskCreate(vTask_Placar_Vida, "Placar Vida", 128, NULL, osPriorityAboveNormal, &tarefa_placar_vida);

	status_movimento_asteroide = xTaskCreate(vTask_Asteroide_Mover, "mov. ast.", 128, NULL, osPriorityNormal, &tarefa_mov_aste);

	status_movimento_nave = xTaskCreate(vTask_Nave_Mover, "mov. nav.", 128, NULL, osPriorityNormal, &tarefa_mov_nave);

	status_colisao = xTaskCreate(vTask_Colisao, "coli", 128, NULL, osPriorityNormal, &tarefa_colisao);

	status_movimento_tiro = xTaskCreate(vTask_Tiro, "mov. tiro", 128, NULL, osPriorityNormal, &tarefa_mov_tiro);

	status_contar_tempo = xTaskCreate(vTask_Contar_Tempo, "tempo tir.", 128, NULL, osPriorityNormal, &tarefa_contar_tempo);


	// flags para debug
	if (status_placar_vida != pdTRUE)
	{
		ST7735_WriteString(0, 0,"Deu Merda Placar.", Font_7x10, ST7735_RED, ST7735_BLACK);
	}
	if (status_display != pdTRUE)
	{
		ST7735_WriteString(0, 24,"Deu Merda Display.", Font_7x10, ST7735_RED, ST7735_BLACK);
	}
	if (status_movimento_asteroide != pdTRUE)
	{
		ST7735_WriteString(0, 36,"Deu Merda Asteroide.", Font_7x10, ST7735_RED, ST7735_BLACK);
	}
	if (status_movimento_nave != pdTRUE)
	{
		ST7735_WriteString(0, 48,"Deu Merda Navo.", Font_7x10, ST7735_RED, ST7735_BLACK);
	}
	if (status_movimento_tiro != pdTRUE)
	{
		ST7735_WriteString(0, 60,"Deu Merda Tiro.", Font_7x10, ST7735_RED, ST7735_BLACK);
	}
	if(status_contar_tempo != pdTRUE)
	{
		ST7735_WriteString(0, 72,"Deu Merda Tempo.", Font_7x10, ST7735_RED, ST7735_BLACK);
	}
	if (status_loop_jogo != pdTRUE)
	{
		ST7735_WriteString(0, 84,"Deu Merda Jogo.", Font_7x10, ST7735_RED, ST7735_BLACK);
	}
	if (status_colisao != pdTRUE)
	{
		ST7735_WriteString(0, 84,"Deu Merda Colisao.", Font_7x10, ST7735_RED, ST7735_BLACK);
	}



    // se não tiver memória disponível o led azul do stm não pisca
    defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 16;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 2;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = 2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, CS_Pin|RST_Pin|DC_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : LED_Pin */
  GPIO_InitStruct.Pin = LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PC15 */
  GPIO_InitStruct.Pin = GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : CS_Pin RST_Pin DC_Pin */
  GPIO_InitStruct.Pin = CS_Pin|RST_Pin|DC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
	while(1)
	{
		  HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
		  osDelay(200);
	}
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM4 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM4) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
