#define __KERNEL__         /* We're part of the kernel */
#define MODULE             /* Not a permanent part, though. */

/* Standard headers for LKMs */
#include <linux/kernel.h>
#include <linux/init.h> 
#include <linux/module.h>
#include <linux/ftrace.h>
#include <linux/ftrace_event.h> 
#include <linux/fs.h>		// for basic filesystem
#include <linux/proc_fs.h>	// for the proc filesystem
#include <linux/seq_file.h>	// for sequence files
#include <linux/jiffies.h>	// for jiffies
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/string.h>

/* Header files required for the linux scheduler functions */
#include <linux/rbtree.h>
#include <linux/sched.h>
#include "/home/ubuntu/linux-3.12.5/kernel/sched/sched.h" /*Specify the path according to your system */
#include <linux/pid.h>
#include <linux/task_work.h> 
#include <trace/events/sched.h>
#include <linux/rbtree_augmented.h>
#include <linux/export.h>

#include <linux/latencytop.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <linux/profile.h>
#include <linux/interrupt.h>
#include <linux/mempolicy.h>
#include <linux/migrate.h>
#include <linux/task_work.h>
#include <trace/events/sched.h>

/* Standard Environmental variables used for this LKM */
#define Q_EMPTY -9999
#define NAN -999999999
#define nr_core 2  
#define nr_cpu 4
#define nr_task 1000 
#define FOUND 1
#define NOT_FOUND 0
#define RUNNABLE 0
#define EXITED 999
#define INFINITE 999999999


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Suhas D Heeraskar");
MODULE_DESCRIPTION("A SMT co-schedule module");

static void (*SMT_schedule)(struct task_struct *prev, struct rq *rq, struct task_struct *sibling_cur, struct rq *sibling_rq);
extern void (*SMT__sched__schedule)(struct task_struct *prev, struct rq *rq, struct task_struct *sibling_cur, struct rq *sibling_rq);

static long int (*SMT_pick_sched_entity)(struct sched_entity *entity);
extern long int (*SMT_pick_next_task)(struct sched_entity *entity);

static struct sched_entity* (*find_next_entity)(struct sched_entity *left);
extern struct sched_entity* (*SMT_find_next_entity)(struct sched_entity *left);

static void SetUpEvent();
static long int ReadCounter();
static int get_task_rq(struct task_struct *task);

//These variables are just for verification.
int sched_history_buffer_actual[4][40];
int sched_hbuff_cpuindex_actual[4] = {0,0,0,0};

static struct proc_dir_entry* sched_debug_file;


spinlock_t SMT_WQlock;
spinlock_t SMT_RQlock;
struct smt_queue{
	int PID;
	struct task_struct *PCB;
	long int MissRate_table[nr_task];
	int this_loc; /*represents index into MissRate_table*/
};

struct smt_queue SMT_Q[nr_core][nr_task]; 
struct task_struct *temp_task;
struct rq *sib_rq;
struct rq *cur_rq;

int head[nr_core]={Q_EMPTY};
int variable[nr_cpu]={0,};

static void SMT_put_prev_task(struct task_struct *prev, struct task_struct *sibling_cur, long int perf_count, int cpu){
	struct smt_queue temp;
	long int sib_ptr=NOT_FOUND, prev_ptr=NOT_FOUND;
	int sib_this_loc, prev_this_loc, _1st_loc=NAN, _2nd_loc=NAN;
	int i,core;
	
	/* Decide to which core, the logical processor belong to*/
	if(cpu<=1){
		core=0; }
	else{
		core=1; }
		
	if(head[core]==Q_EMPTY){	
		_1st_loc = 0; _2nd_loc = 1;
		/* Empty list. Store the *prev and *sibling_cur both. */
		temp.PID = prev->pid;
		temp.PCB = prev;
		temp.this_loc = _1st_loc;
		prev_ptr = temp.this_loc;
		SMT_Q[core][temp.this_loc]=temp;
		
		temp.PID = sibling_cur->pid;
		temp.PCB = sibling_cur;
		temp.this_loc = _2nd_loc;
		sib_ptr = temp.this_loc;
		SMT_Q[core][temp.this_loc] = temp;
		
		/* Update the MissRate_table content */
		SMT_Q[core][prev_ptr].MissRate_table[sib_ptr] = perf_count;
		SMT_Q[core][sib_ptr].MissRate_table[prev_ptr] = perf_count;
		head[core] = core;
		
		return;
	}
	else{   
		_1st_loc=NAN; _2nd_loc=NAN;
		sib_ptr=NOT_FOUND; prev_ptr=NOT_FOUND;
		
		/* Find the location of *prev and *sibling_cur */
		for(i=0;i<nr_task;i++){
			if(_1st_loc==NAN && (SMT_Q[core][i].PID==NAN || !SMT_Q[core][i].PCB)) _1st_loc = i;
			if(_2nd_loc==NAN && _1st_loc!=i && _1st_loc!=NAN && SMT_Q[core][i].PID==NAN) _2nd_loc=i;
			if(SMT_Q[core][i].PID == sibling_cur->pid){
				sib_this_loc = SMT_Q[core][i].this_loc;
				sib_ptr = FOUND; // sib_ptr represents the sibling_cur location in SMT_Q
			}
			if(SMT_Q[core][i].PID == prev->pid){
				prev_this_loc = SMT_Q[core][i].this_loc;
				prev_ptr = FOUND; //prev_ptr represents the prev location in SMT_Q
			}
		}
		
		/* If both PCBs exist in the data structure update them */
		if(sib_ptr==FOUND && prev_ptr==FOUND){
			
			SMT_Q[core][prev_ptr].MissRate_table[sib_this_loc] = perf_count;
			SMT_Q[core][sib_ptr].MissRate_table[prev_this_loc] = perf_count;
			
			return;
		}
		else if(prev_ptr==NOT_FOUND && sib_ptr==FOUND){
			if(_1st_loc==NAN || _1st_loc > 998){

				return;
			}
			temp.PID = prev->pid;
			temp.PCB = prev;
			temp.this_loc = _1st_loc;
			temp.MissRate_table[sib_ptr] = perf_count;
			SMT_Q[core][temp.this_loc] = temp;
			
			/* Update in *sibling_cur */
			SMT_Q[core][sib_ptr].MissRate_table[temp.this_loc] = perf_count;
			
			return;
		}
		else if(sib_ptr==NOT_FOUND && prev_ptr==FOUND){
			if(_1st_loc==NAN || _1st_loc > 998){
			
				return;
			}
			temp.PID = sibling_cur->pid;
			temp.PCB = sibling_cur;
			temp.this_loc = _1st_loc;
			temp.MissRate_table[prev_ptr] = perf_count;
			SMT_Q[core][temp.this_loc] = temp;
			
			/* Update in *prev */
			SMT_Q[core][prev_ptr].MissRate_table[temp.this_loc] = perf_count;
			
			return;
		}
		else if(sib_ptr==NOT_FOUND && prev_ptr==NOT_FOUND) {
		        if(_1st_loc==NAN || _2nd_loc==NAN || _1st_loc >997 || _2nd_loc > 998){
				return;
			}
		        temp.PID = prev->pid;
			temp.PCB = prev;
			temp.this_loc = _1st_loc;
			prev_ptr = temp.this_loc;
			SMT_Q[core][temp.this_loc] = temp;
			
			temp.PID = sibling_cur->pid;
			temp.PCB = sibling_cur;
			temp.this_loc = _2nd_loc;
			sib_ptr = temp.this_loc;
			SMT_Q[core][temp.this_loc] = temp;
			
			/* Update the MissRate_table contents of *prev and *sibling_cur */
			SMT_Q[core][prev_ptr].MissRate_table[sib_ptr] = perf_count;
			SMT_Q[core][sib_ptr].MissRate_table[prev_ptr] = perf_count;
			
			return;
		}
	}
}

/* Procedure to get the runqueue on which *task is currently attached */
static int get_task_rq(struct task_struct *task){
	struct cfs_rq *cfs_rq;
	struct sched_entity *se;
	struct rq *rq;
	int temp;
	
	if(!task){return NAN;}
	se = &task->se;
	if(!se){ 
		return NAN; }
	cfs_rq = se->cfs_rq;
	if(!cfs_rq){ 
		return NAN; }
	rq = cfs_rq->rq;
	if(!rq){ 
		return NAN; }
	temp = cpu_of(rq);
	return temp;
}


static void CleanUP_SMT_Q(int cpu, int sibling_cpu){
	int core,i,j, count;
	int rq;
	
	if(cpu<=1){
		core=0; }
	else{
		core=1; }
	
	if(head[core]==Q_EMPTY){
		
		return;
	}
	
	/* Remove the entries of task which are not present in the system and those who have migrated from their CPUs. */
	count=0;
	for(i=0;i<nr_task;i++){
		if(SMT_Q[core][i].PID!=NAN && SMT_Q[core][i].PCB){
		if(pid_task(find_vpid(SMT_Q[core][i].PID),PIDTYPE_PID)){
			rq = get_task_rq(SMT_Q[core][i].PCB);
			if((rq!=cpu && rq!=sibling_cpu)){
		
				SMT_Q[core][i].PID = NAN;
				SMT_Q[core][i].PCB = NULL;
				for(j=0;j<nr_task;j++){
					SMT_Q[core][j].MissRate_table[i]=INFINITE;
				}
				for(j=0;j<nr_task;j++){
					SMT_Q[core][i].MissRate_table[j]=INFINITE;
				}
				SMT_Q[core][i].this_loc = NAN;
				count++;
			}
		}
		else{
		
			SMT_Q[core][i].PID = NAN;
			SMT_Q[core][i].PCB = NULL;
			for(j=0;j<nr_task;j++){
				SMT_Q[core][j].MissRate_table[i]=INFINITE;
			}
			for(j=0;j<nr_task;j++){
				SMT_Q[core][i].MissRate_table[j]=INFINITE;
			}
			SMT_Q[core][i].this_loc = NAN;
			count++;
		}
		}
		
	}
	if(count==nr_task)
		head[core]=Q_EMPTY;

	return;
}


/* 
	We know that *sibling_cur is being run in the sibling logical processor. 
We are looking for the most compatible task for *sibling_cur to run in this 
CPU. If we find any such PID, it is presumed that PCB of PID is mostly in 
current cfs_rq than in any other cfs_rq. So first look at current cfs_rq 
to find the task. If not present, it may be present in other cfs_rq. Hence use 
default CFS scheduler to schedule the task. 
*/

static long int local__SMT_pick_next_task(struct sched_entity *entity){
	int core, i, sib_ptr, sib_this_loc, next_loc;
	struct task_struct *check, *sibling_cur;
	struct cfs_rq *cfs;
	struct rq *rq;
	
	spin_lock(&SMT_WQlock); 
	if(!entity){
		spin_unlock(&SMT_WQlock);
		return -1;
	}
	cfs = entity->cfs_rq;
	rq = cfs->rq;
	sibling_cur = sib_rq->curr;	
	
	/* Find SMT_Q[i] */ 
	if(rq->cpu<=1){
		core=0; }
	else{
		core=1; }
	

	/* If SMT_Q empty */
	if(head[core]==Q_EMPTY){
		spin_unlock(&SMT_WQlock);
		return -1;
	}
	
	/* If SMT_Q is not empty search for sibling_cur location */
	sib_ptr = NAN; sib_this_loc = NAN;
	
	if(!sibling_cur){
		spin_unlock(&SMT_WQlock);
		return -1;}
	for(i=0;i<nr_task;i++){
		if(SMT_Q[core][i].PID == sibling_cur->pid)
		if(SMT_Q[core][i].PCB){
				sib_this_loc = SMT_Q[core][i].this_loc;
				sib_ptr = i; // sib_ptr represents the sibling_cur location in SMT_Q
				break;
		}
	}
	
	if(sib_ptr==NAN || sib_this_loc==NAN){
		spin_unlock(&SMT_WQlock);
		return -1; // run default pick_task
	}
	else{ 
		check = container_of(entity, struct task_struct, se);
		if(!check){
			spin_unlock(&SMT_WQlock);
			return -1;	
		}
		if(check->pid==0){spin_unlock(&SMT_WQlock); return -1;}
		next_loc = NAN;
		for(i=0;i<nr_task;i++){
			if(SMT_Q[core][i].PCB)
				if(check->pid==SMT_Q[core][i].PCB->pid){
					if(SMT_Q[core][i].MissRate_table[sib_this_loc]<INFINITE)
						next_loc=i;
				}
		}
		if(next_loc==NAN){
			spin_unlock(&SMT_WQlock);
			return -1;
		}
		
	}
	spin_unlock(&SMT_WQlock);
	return SMT_Q[core][next_loc].MissRate_table[sib_this_loc]; // return miss rate.
}


static struct sched_entity* SMT__pick_next_entity(struct sched_entity *se){
        struct rb_node *next = rb_next(&se->run_node);

        if (!next)
                return NULL;

        return rb_entry(next, struct sched_entity, run_node);
}

long int miss[nr_cpu]={INFINITE,};
int dont_run[nr_cpu];
long int nr_switches[nr_cpu]={0,};
char leftmost[nr_cpu][20];
char smt_next[nr_cpu][20];
long int oth_cfs_swtch[nr_cpu]={0,};
long int cfs_swtch[nr_cpu]={0,};
long int smt_swtch[nr_cpu]={0,};
long int context_swtch[nr_cpu]={0,};

static struct sched_entity* __SMT_find_next_entity(struct sched_entity *left)
{	struct sched_entity *SMT_temp,*SMT_next=NULL;
	int flag;
	long int temp;
	struct task_struct *p,*next;
	struct cfs_rq *cfs;
	struct rq *rq;
	
	p = container_of(left,struct task_struct, se);
	if(!p){ trace_printk("Left_most:NULL.\n"); return NULL;}
	//trace_printk("left_most:%s\n",p->comm); 
	
	cfs = left->cfs_rq;
	rq = cfs->rq;
	strcpy(leftmost[rq->cpu],p->comm);
	//trace_printk("PID:%d,left_most[%d]:%s\n",p->pid,rq->cpu,leftmost[rq->cpu]);
	miss[rq->cpu]=INFINITE;temp=-1;
	for(flag=1;flag<10;flag++){
		SMT_temp=SMT__pick_next_entity(left);
		if(!SMT_temp){
		//	trace_printk("No inorder_suc after:%d.\n",flag);
			break;
		}
		temp = local__SMT_pick_next_task(SMT_temp);
		
		if(temp!=-1 && temp < miss ){
		 	miss[rq->cpu] = temp;
			SMT_next = SMT_temp;
			dont_run[rq->cpu]=1;
		}
	/*	p = container_of(SMT_temp,struct task_struct, se);
		if(!p){ trace_printk("p:NULL.\n"); return NULL;}
		trace_printk("p:%d,inorder#:%d,temp_miss:%ld\n",p->pid,flag,temp); */
		left=SMT_temp;
	}
	if(!SMT_next){ //trace_printk("SMT_next entity:NULL.\n"); 
		return NULL;}
	next = container_of(SMT_next,struct task_struct, se);
	if(!next){ trace_printk("task of SMT_next:NULL.\n"); return NULL;}
	/*trace_printk("SMT_next:%d,my_position:%d,miss[%d]:%ld\n",next->pid,my_pos,rq->cpu,miss[rq->cpu]);  */
	strcpy(smt_next[rq->cpu],next->comm);
	return SMT_next; 
}

/* Function to pick co-schedulable task */
static long int __SMT_pick_next_task(struct sched_entity *entity)
{	int core, i, sib_ptr, sib_this_loc, next_loc;
	struct task_struct *check, *sibling_cur;
	struct cfs_rq *cfs;
	struct rq *rq;
	
	
	/*This is to make sure that __SMT_pick_next_task wont be executed unnecessarily according to our design.
	Before deleting this plz check pick_next_entity function of fair.c for control flow. There, if you notice,
	SMT_pick_next_task is called again in SMT_find_next_entity. In fact both are clones with different names.
	Chances are more that the same task can be done twice in some situations.
	*/
		
	cfs = entity->cfs_rq;
	rq = cfs->rq;
	sibling_cur = sib_rq->curr;	
	
	if(dont_run[rq->cpu]==1){
		dont_run[rq->cpu]=0;
		if(miss[rq->cpu]<INFINITE)
			return miss[rq->cpu];
		else
			return -1;
	}
	
	spin_lock(&SMT_WQlock);
	if(!entity){		
		spin_unlock(&SMT_WQlock);
		return -1;
	}
	
	//CleanUP_SMT_Q(rq->cpu, sib_rq->cpu);
	
	/* Find SMT_Q[i]  */
	if(rq->cpu<=1){
		core=0; }
	else{
		core=1; }
	

	/* If SMT_Q empty */
	if(head[core]==Q_EMPTY){
		spin_unlock(&SMT_WQlock);
		return -1;
	}
	
	/* If SMT_Q is not empty search for sibling_cur location */
	sib_ptr = NAN; sib_this_loc = NAN;
	
	if(!sibling_cur){
		spin_unlock(&SMT_WQlock);
		return -1;}
	for(i=0;i<nr_task;i++){
		if(SMT_Q[core][i].PID == sibling_cur->pid)
		if(SMT_Q[core][i].PCB){
				sib_this_loc = SMT_Q[core][i].this_loc;
				sib_ptr = i; // sib_ptr represents the sibling_cur location in SMT_Q
				break;
		}
	}
	
	if(sib_ptr==NAN || sib_this_loc==NAN){
		
		spin_unlock(&SMT_WQlock);
		return -1; // run default pick_task
	}
	else{ 
		check = container_of(entity, struct task_struct, se);variable[rq->cpu]++;
		if(!check){
		
			spin_unlock(&SMT_WQlock);
			return -1;	
		}
		next_loc = NAN;
		for(i=0;i<nr_task;i++){
			if(SMT_Q[core][i].PCB)
				if(check->pid==SMT_Q[core][i].PCB->pid){
					if(SMT_Q[core][i].MissRate_table[sib_this_loc]<INFINITE)
						next_loc=i;
				}
		}
		if(next_loc==NAN){
			spin_unlock(&SMT_WQlock);
			return -1;
		}
	}
	spin_unlock(&SMT_WQlock);
	return SMT_Q[core][next_loc].MissRate_table[sib_this_loc]; // return miss rate.
}

/* Function to set up event for performance monitor counter */
static void SetUpEvent(){
	int reg_addr=0x186; /* EVTSEL0. */
	int event_no=0x0024; /* L2 cache miss. */
	int umask=0x3F00; /* Demand data requests that missed L2 cache. */
	int enable_bits=0x430000; /* All the other control bits. */
	int event=event_no | umask | enable_bits;
	
	/*    Write to MSR    register addr     low        high*/
	__asm__ ("wrmsr" : : "c"(reg_addr), "a"(event), "d"(0x00));
	
	return;
}

/* Read the performance monitor counter */
static long int ReadCounter(){
	long int count;
	long int eax_low, edx_high;
	int reg_addr=0xC1; /* Regester address of PERFCOUNT0 */
	
	/*   Read from MSR     low               high       register addr*/
	__asm__("rdmsr" : "=a"(eax_low), "=d"(edx_high) : "c"(reg_addr));
	count = ((long int)eax_low | (long int)edx_high<<32);
	
	return count;
}


/* The actual implementation of SMT_sched_schedule */
static void SMT_schedule_implementation(struct task_struct *prev, struct rq *rq, struct task_struct *sibling_cur, struct rq *sibling_rq)
{
	long int perf_count,curCount;
	static long int prevCount[nr_cpu]={0,};
	int CPU[nr_cpu]={0,}, cpu;
	
	cpu=rq->cpu;
	/*Setup the event only once on each logical cpu */
	if(CPU[cpu]==0){
		SetUpEvent();
		CPU[cpu]=cpu+1;
	}
	curCount=ReadCounter();
	perf_count = curCount - prevCount[cpu]; /* the delta of L2 cache misses */
	prevCount[cpu] = curCount;
	
	sib_rq = sibling_rq;
	cur_rq = rq;
	
	context_swtch[rq->cpu]=context_swtch[rq->cpu]+1;
	if( !strcmp(prev->comm,"gcc_base.cpu200") || !strcmp(prev->comm,"hmmer_base.cpu2") ){ // || !strcmp(prev->comm,"gcc_base.cpu200") || !strcmp(prev->comm,"hmmer_base.cpu2") || !strcmp(prev->comm,"libquantum_base") || !strcmp(prev->comm,"mcf_base.cpu200") ) { 
		if( strcmp(smt_next[rq->cpu],"hmmer_base.cpu2")==0 ){
			smt_swtch[rq->cpu]=smt_swtch[rq->cpu]+1;
			
			if( strcmp(leftmost[rq->cpu],"hmmer_base.cpu2")!=0 )
				cfs_swtch[rq->cpu]=cfs_swtch[rq->cpu]+1;
		
		}
		
		if( strcmp(smt_next[rq->cpu],"gcc_base.cpu200")==0 ){
			smt_swtch[rq->cpu]=smt_swtch[rq->cpu]+1;
			
			if( strcmp(leftmost[rq->cpu],"gcc_base.cpu200")!=0 )
				cfs_swtch[rq->cpu]=cfs_swtch[rq->cpu]+1;
		
		}
		
		nr_switches[rq->cpu]=nr_switches[rq->cpu]+1;
	 
	} 
	
	spin_lock(&SMT_WQlock);
	variable[cpu]=0;
	CleanUP_SMT_Q(rq->cpu, sibling_rq->cpu);
	SMT_put_prev_task(prev, sibling_cur, perf_count, cpu);
	spin_unlock(&SMT_WQlock);
  	return;
}


/* This function is very much associated with the two arrays declared above */
static int sched_debug_show(struct seq_file *m, void *v){ //seq_printf(m,"const. string",arguements);
	int cpu;
	for(cpu=0;cpu<nr_cpu;cpu++){
		seq_printf(m,"CPU:%d,CFS_sched:%ld,SMT_sched:%ld,Tot_sched:%ld,nr_contxt_swtch:%ld\n",cpu,cfs_swtch[cpu],smt_swtch[cpu],nr_switches[cpu],context_swtch[cpu]);
	}
	return 0;
}

static int sched_debug_open(struct inode *inode, struct file *file){
     	return single_open(file, sched_debug_show, NULL);
}

/*Data structure for sched_debug operations */
static const struct file_operations sched_debug_fops = {
     	.owner		= THIS_MODULE,
     	.open		= sched_debug_open,
     	.read		= seq_read,
     	.llseek		= seq_lseek,
     	.release	= single_release,
};

/*Function definition to create /proc entry. Name of the entry in /proc is "sched_debug" */
static int sched_debug_proc_init(void){
     	sched_debug_file = proc_create("SMT_schedDebug", 0, NULL, &sched_debug_fops);
     	if (!sched_debug_file) {
         	return -ENOMEM;
     	}
     	return 0;
}


int __init SMT_loadable_module(void){
	int i,j,k;
	
	/* Reset the counter register */
	__asm__ ("wrmsr" : : "c"(0xC1), "a"(0x00), "d"(0x00));
	
	/* Initiate the SMT_Q vector */
	for(i=0;i<nr_core;i++)
		for(j=0;j<nr_task;j++){
			SMT_Q[i][j].PID = NAN;
			SMT_Q[i][j].PCB = NULL;
			for(k=0;k<nr_task;k++){
				SMT_Q[i][j].MissRate_table[k]=INFINITE;
			}
			SMT_Q[i][j].this_loc = NAN;
		}
			
	/* First job to do upon loading kernel is to create an entry in proc file system */
	sched_debug_proc_init();  /*Change this for appropriate return code*/

	/* Spin lock initiate */
	spin_lock_init(&SMT_WQlock);
	spin_lock_init(&SMT_RQlock);
	
	/* Store the original link to the function call into SMT_schedule and replace the original 
	   link by new address SMT_schedule_implementaion */
	SMT_schedule = SMT__sched__schedule;
	SMT__sched__schedule = &SMT_schedule_implementation;
	
	SMT_pick_sched_entity = SMT_pick_next_task;
	SMT_pick_next_task = &__SMT_pick_next_task;
	
	find_next_entity = SMT_find_next_entity;
	SMT_find_next_entity = &__SMT_find_next_entity;
	 return 0;
}

/* Cleanup - undo whatever init_module did */
void __exit cleanup_module(void){
		
	/*Store back the original address of function call to SMT_sched_schedule*/
	SMT__sched__schedule = SMT_schedule;
	SMT_pick_next_task = SMT_pick_sched_entity;
	SMT_find_next_entity = find_next_entity; 
	remove_proc_entry("SMT_schedDebug", NULL); /* function to remove proc entry */ 
	return;
}

module_init(SMT_loadable_module);
