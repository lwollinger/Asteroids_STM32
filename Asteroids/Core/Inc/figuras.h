#ifndef __FIGURAS_H__
#define __FIGURAS_H__

#include <stdint.h>

// estrutura para definicao de uma figura simples monocolor
typedef struct {
    const 	uint8_t width;
    		uint8_t height;
    const 	uint16_t *data;
} FigDef;


extern FigDef pirata;

extern FigDef nave;
extern FigDef asteroide2;
extern FigDef asteroide3;
extern FigDef asteroide4;

extern FigDef tiro;

#endif
