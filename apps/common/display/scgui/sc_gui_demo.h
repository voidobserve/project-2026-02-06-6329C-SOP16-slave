#ifndef _SC_GUI_DEMO_H_
#define	_SC_GUI_DEMO_H_

#include "sc_gui.h"
#include "sc_obj_widget.h"
#include "sc_event_task.h"


void* get_screen(void);
void demo_main_task(void *arg);
void demo_adc_task(void *arg);
void demo_loop(void);


#endif // _SC_GUI_DEMO_H_


