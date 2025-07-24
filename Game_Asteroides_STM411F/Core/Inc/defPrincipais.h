#ifndef DEFPRINCIPAIS_H_
#define DEFPRINCIPAIS_H_

// Defini��o para uso de atraso no FreeRTOS
#define MS(tempo)		tempo/portTICK_RATE_MS

//Defini��es de macros para o trabalho com bits

#define BIT(x)			1<<x

#define	SET_BITt(y,bit)	(y|=(1<<bit))	//coloca em 1 o bit x da vari�vel Y
#define	CLR_BIT(y,bit)	(y&=~(1<<bit))	//coloca em 0 o bit x da vari�vel Y
#define CPL_BIT(y,bit) 	(y^=(1<<bit))	//troca o estado l�gico do bit x da vari�vel Y
#define TST_BIT(y,bit) 	(y&(1<<bit))	//retorna 0 ou 1 conforme leitura do bit

#endif /* DEFPRINCIPAIS_H_ */
