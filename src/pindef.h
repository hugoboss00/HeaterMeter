#ifndef __PINDEF_H__
#define __PINDEF_H__

#define PIN_DEFINE(namehm,definehm,definebbbio) namehm = definehm,

enum {
#include "pindef.def"
};





#endif /* __PINDEF_H__ */
