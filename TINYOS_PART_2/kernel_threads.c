
#include "tinyos.h"
#include "kernel_proc.h"
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_streams.h"

void start_main_thread_new()
{
  Task call = cur_thread()->ptcb->task;
  int argl = cur_thread()->ptcb->argl;
  void* args = cur_thread()->ptcb->args;
  int exitval = call(argl,args);
  ThreadExit(exitval);
}
    
/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
       
  PTCB* ptcb = (PTCB*)xmalloc(sizeof(PTCB));
  ptcb->task = task;
  ptcb->refcount = 0;
  ptcb->exited = 0;
  ptcb->argl = argl;
  ptcb->detached = 0;
  ptcb->args = args;
  ptcb->exit_cv = COND_INIT;

  TCB* tcb = spawn_thread(CURPROC, start_main_thread_new);
    
  ptcb->tcb = tcb;
  tcb->ptcb = ptcb;

  CURPROC->thread_count++;

  rlnode_init(&ptcb->ptcb_list_node,ptcb);
  rlist_push_back(& CURPROC->ptcb_list ,& ptcb->ptcb_list_node);

  wakeup(ptcb->tcb);

  return (Tid_t) ptcb;
}


/**
  @brief Return the Tid of the current thread.
 */
Tid_t sys_ThreadSelf()
{
	return (Tid_t) cur_thread()->ptcb;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{

  PTCB* ptcb = (PTCB*)tid;

  if(tid == sys_ThreadSelf()){
    return -1;
  }

  if(rlist_find(& CURPROC->ptcb_list,ptcb,NULL) == NULL)
    return -1 ;

  if(ptcb->detached == 1){
    return -1;
  }

  ptcb->refcount++;

  while(ptcb->exited == 0 && ptcb->detached == 0){
    kernel_wait(&ptcb->exit_cv,SCHED_USER);
  }

  ptcb->refcount--;

  if(ptcb->detached==1){
    return -1;
  }

  if(exitval!= NULL){
    *exitval = ptcb->exitval;
  }

  if(ptcb->refcount == 0){
    rlist_remove(&ptcb->ptcb_list_node);
    free(ptcb);
  }

	return 0;
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
  PTCB* ptcb = (PTCB*)tid;

  if(rlist_find(& CURPROC->ptcb_list,ptcb,NULL) == NULL)
    return -1 ;

  if (ptcb->exited == 1) 
    return -1;

  ptcb->detached = 1;
  kernel_broadcast(&ptcb->exit_cv);
  
  return 0;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

    PCB *curproc = CURPROC;
    PTCB* ptcb = cur_thread()->ptcb;

    ptcb->exitval = exitval;
    ptcb->exited = 1;

    kernel_broadcast(&ptcb->exit_cv);
    CURPROC->thread_count --;

    if(curproc -> thread_count == 0){

      if(get_pid(curproc)!=1) {
   /* Reparent any children of the exiting process to the 
       initial task */
        PCB* initpcb = get_pcb(1);

      while(!is_rlist_empty(& curproc->children_list)) {
        rlnode* child = rlist_pop_front(& curproc->children_list);
        child->pcb->parent = initpcb;
        rlist_push_front(& initpcb->children_list, child);
      }

      /* Add exited children to the initial task's exited list 
       and signal the initial task */
      if(!is_rlist_empty(& curproc->exited_list)) {
        rlist_append(& initpcb->exited_list, &curproc->exited_list);
        kernel_broadcast(& initpcb->child_exit);
      }

      /* Put me into my parent's exited list */
      rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
      kernel_broadcast(& curproc->parent->child_exit);
      }
      assert(is_rlist_empty(& curproc->children_list));
      assert(is_rlist_empty(& curproc->exited_list));


  /* 
    Do all the other cleanup we want here, close files etc. 
   */

  /* Release the args data */
  if(curproc->args) {
    free(curproc->args);
    curproc->args = NULL;
  }

  /*Clean up FIDT */
  for(int i=0;i<MAX_FILEID;i++) {
    if(curproc->FIDT[i] != NULL) {
      FCB_decref(curproc->FIDT[i]);
      curproc->FIDT[i] = NULL;
    }
  } 

  while(!is_rlist_empty(&CURPROC->ptcb_list)){
    PTCB* check = rlist_pop_front(&CURPROC->ptcb_list)->ptcb;
    if(check != NULL){
      free(check);
    }
  }

  /* Disconnect my main_thread */
  curproc->main_thread = NULL;

  /* Now, mark the process as exited. */
  curproc->pstate = ZOMBIE;
  
 }
 /* Bye-bye cruel world */
  kernel_sleep(EXITED, SCHED_USER);
}