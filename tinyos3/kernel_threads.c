
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"
#include "kernel_streams.h"
#include "kernel_cc.h"

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
  TCB* tcb;
  tcb=spawn_thread(CURPROC,help_create_thread);
  create_ptcb(tcb,task,argl,args);
  CURPROC->thread_count++;
  wakeup(tcb);
  return (Tid_t)tcb->ptcb;
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
int sys_ThreadJoin(Tid_t tid, int* exitval){

  PTCB *ptcb=(PTCB *)tid;
  //rlnode find gia to an einai sto ptcb list
   
  if (rlist_find(&CURPROC->ptcb_list,ptcb,NULL)==NULL) {
    return -1;
  }

  if (ptcb->detached==1) {
    return -1;
  }
  
  if(ptcb==cur_thread()->ptcb){
    return -1;
  }

  refcount_increase(ptcb);

  while(ptcb->exited==0 && ptcb->detached==0){
      kernel_wait(&(ptcb->exit_cv),SCHED_USER);
  }
  
  
  
  if(ptcb->detached==1){
	  ptcb->refcount--;
    //refcount_decrease(ptcb);
    return -1;
  }

  if(exitval!=NULL){
    *exitval=ptcb->exitval;
  }
  refcount_decrease(ptcb);
  return 0;

}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid){

  PTCB *ptcb=(PTCB *)tid;

  
  if (rlist_find(&CURPROC->ptcb_list,ptcb,NULL)==NULL){
    return -1;
  }

  if (ptcb->exited==1){
    return -1;
  }
 
    ptcb->detached=1;
    /*ptcb->refcount=0;*/
    kernel_broadcast(&(ptcb->exit_cv));
    
  return 0;

}


/*
  @brief Terminate the current thread.
  */

void sys_ThreadExit(int exitval)
{
  PTCB *ptcb=cur_thread()->ptcb;
  PCB* curproc=CURPROC;
  ptcb->exitval=exitval;
  ptcb->exited=1;
  kernel_broadcast(&(ptcb->exit_cv));



  curproc->thread_count--;



  if(curproc->thread_count==0){
    //sth if(get_pid(cur_proc())!=1)
      if(get_pid(curproc)!=1){
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

  /* Clean up FIDT */
    for(int i=0;i<MAX_FILEID;i++) {
      if(curproc->FIDT[i] != NULL) {
        FCB_decref(curproc->FIDT[i]);
        curproc->FIDT[i] = NULL;
      }
    }


 
    rlnode *temp;

    while(!is_rlist_empty(&(curproc->ptcb_list))){
      temp=rlist_pop_front(&(curproc->ptcb_list));
      free(temp->ptcb);

    }
  



  /* Disconnect my main_thread */
    curproc->main_thread = NULL;

  /* Now, mark the process as exited. */
    curproc->pstate = ZOMBIE;

  /* Bye-bye cruel world */
   
  }
  kernel_sleep(EXITED, SCHED_USER);
}



