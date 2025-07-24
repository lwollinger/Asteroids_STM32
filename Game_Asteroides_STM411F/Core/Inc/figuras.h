#ifndef __FIGURAS_H__
#define __FIGURAS_H__

#include <stdint.h>

// estrutura para definicao de uma figura simples monocolor
typedef struct {
     	uint8_t width;
    		uint8_t height;
     	uint16_t *data;
} FigDef;


//extern FigDef fig_campo_minado;
//extern FigDef soldado;
extern  FigDef tela_inicial_figura;
extern  FigDef tela_inicial_figura2;
extern  FigDef tela_asteroide_1;
extern  FigDef tela_asteroide_2;
extern  FigDef tela_asteroide_3;
extern  FigDef tela_asteroide_4;
extern  FigDef tela_nave_0;
extern  FigDef tela_nave_90;
extern  FigDef tela_nave_180;
extern  FigDef tela_nave_270;
extern  FigDef tela_tiro;
extern  FigDef zero_tela;

#endif
