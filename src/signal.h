//
// Created by wong on 10/27/18.
//

#ifndef TRANSOCKS_WONG_SIGNAL_H
#define TRANSOCKS_WONG_SIGNAL_H

#include <signal.h>

#include "context.h"


int signal_init(transocks_global_env *);

void signal_deinit(transocks_global_env *);

#endif //TRANSOCKS_WONG_SIGNAL_H
