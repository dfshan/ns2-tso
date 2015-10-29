#ifndef lint
static const char rcsid[] =
    "@(#) $Header: /cvsroot/nsnam/ns-2/queue/enhanced-shared-memory.cc,v 1.0 2014/04/10 20:00:00 haldar Exp $ (LBL)";
#endif

#include "enhanced-shared-memory.h"

// initialize the static members
EnhancedSharedMemory* EnhancedSharedMemory::all_queue[MAX_LINK_NUM] = {NULL};
int EnhancedSharedMemory::queue_num = 0;
int EnhancedSharedMemory::edt_num = 0;

static class EnhancedSharedMemoryClass : public TclClass {
 public:
	EnhancedSharedMemoryClass() : TclClass("Queue/EnhancedSharedMemory") {}
	TclObject* create(int, const char*const*) {
		return (new EnhancedSharedMemory);
	}
} class_enhanced_shared_memory;


int EnhancedSharedMemory::adj_counters()
{
	double now_time = NOW_TIME;
	double elasped_time1 = now_time - trigger_time1;
	double elasped_time2 = now_time - trigger_time2;

	// adjust timer1: the traffic is not bursty
	if(trigger_time2 < 0 && elasped_time1 >= INIT_TIMER1) {
		counter1 = 0;
		counter2 = 0;
		trigger_time1 = now_time;
	}

	// adjust timer2: timeout, change to controlled state
	if(trigger_time2 >= 0 && elasped_time2 >= INIT_TIMER2) {
		to_controlled();
	}

	return (trigger_time2 >= 0 ? 0 : 1);
}

/*
 * drop-tail
 */
void EnhancedSharedMemory::printque(char pre)
{
	if (DISQUE < 0) {
		return ;
	} else if (DISQUE == 0 && pre != 'd') {
		return ;
	}
	int quelen = get_occupied_mem(queue_id);
	// <flag> <time in s> <queue id> <queue length in pkts> <occupied buffer size in pkts>
	printf("%c %f %d %d %d %.3f %d\n", pre, NOW_TIME/1000.0, queue_id, all_queue[queue_id] -> q_->length(), quelen, 1.0*quelen/BUFFER_SIZE, edt_num);
}

void EnhancedSharedMemory::enque(Packet* p) {
	double threshold = adj_counters()?get_threshold(queue_id):(1.0 * BUFFER_SIZE/edt_num);
	/*0: no drop; 1: drop and qlen > threshold  2: drop and buffer overflow*/
	int is_drop = 0;
	if (get_occupied_mem(queue_id) > BUFFER_SIZE) {
		drop(p);
		printque('d');
		is_drop = 2;
	} else if (q_->length() > threshold) {
		drop(p);
		printque('d');
		is_drop = 1;
	} else {
		q_->enque(p);
		printque('+');
	}
	switch (is_drop) {
		case 0:
			counter1 = 0;
			/*Change to the uncontrolled state*/
			if (trigger_time2 < 0 && ++counter2 >= COUNTER2) {
				to_uncontrolled();
			}
			//if (queue_id == 3)
			//	printf("time:%.8f enqueue counter1:%d counter2:%d trigger_time1:%.2f trigger_time2: %.2f\n", NOW_TIME, counter1, counter2, trigger_time1, trigger_time2);
			break;
		case 1:
			counter1 = 0;
			counter2 = 0;
			break;
		case 2:
			/*Buffer overflows, change all ports to controlled state*/
			to_controlled_all();
			//if (queue_id == 3)
			//	printf("time:%.8f dropped counter1:%d counter2:%d trigger_time1:%.2f trigger_time2: %.2f\n", NOW_TIME, counter1, counter2, trigger_time1, trigger_time2);
			break;
	}
}

Packet* EnhancedSharedMemory::deque()
{
	adj_counters();

	Packet* result = q_->deque();
	int quelen = get_occupied_mem(queue_id);
	printque('-');
	if( result != 0 ) {
		counter1 ++;
		if (trigger_time2 >= 0) {
			/*Port becomes underloaded. Change to the controlled state*/
			if (counter1 >= COUNTER1) {
				to_controlled();
			}
		} else {
			counter2 --;
			counter2 = (counter2 < 0 ? 0 : counter2);
		}
	}
	//if (queue_id == 3)
	//	printf("time:%.8f dequeue counter1:%d counter2:%d trigger_time1:%.2f trigger_time2: %.2f\n", NOW_TIME, counter1, counter2, trigger_time1, trigger_time2);
	return result;
}

int EnhancedSharedMemory::get_occupied_mem(int id)
{
	// for this switch, the ports' id are start, start+1, ..., start + SWITCH_PORTS
	int start = (id / SWITCH_PORTS) * SWITCH_PORTS;
	int occupy = 0;
	for(int i = start; i < start + SWITCH_PORTS && i < queue_num; i++ ) {
		occupy += all_queue[i] -> q_->length();
	}
	return occupy;
}

double EnhancedSharedMemory::get_threshold(int id)
{
	double threshold = get_occupied_mem(id);
	threshold = ALPHA * (BUFFER_SIZE - threshold);
	return threshold > 0 ? threshold : 0;
}

void EnhancedSharedMemory::to_controlled() {
	if (trigger_time2 >= 0 ) {
		counter1 = 0;
		counter2 = 0;
		trigger_time2 = -1;
		trigger_time1 = NOW_TIME;
		edt_num --;
	}
}

void EnhancedSharedMemory::to_uncontrolled() {
	if (trigger_time2 < 0 ) {
		long now_time = NOW_TIME;
		trigger_time2 = now_time;
		trigger_time1 = now_time;
		edt_num ++;
		counter2 = 0;
		counter1 = 0;
	}
}

void EnhancedSharedMemory::to_controlled_all() {
	// for this switch, the ports' id are start, start+1, ..., start + SWITCH_PORTS
	int start = (queue_id / SWITCH_PORTS) * SWITCH_PORTS;
	for(int i = start; i < start + SWITCH_PORTS && i < queue_num; i++ ) {
		all_queue[i] -> to_controlled();
	}
}
