#include "dsh.h"

job_t *start_job = NULL;

void seize_tty(pid_t callingprocess_pgid); /* Grab control of the terminal for the calling process pgid.  */
void continue_job(job_t *j); /* resume a stopped job */
void spawn_job(job_t *j, bool fg); /* spawn a new job */

/* Sets the process group id for a given job and process */
int set_child_pgid(job_t *j, process_t *p)
{
    if (j->pgid < 0) /* first child: use its pid for job pgid */
        j->pgid = p->pid;
    return(setpgid(p->pid,j->pgid));
}

/* Creates the context for a new child by setting the pid, pgid and tcsetpgrp */
void new_child(job_t *j, process_t *p, bool fg)
{
         /* establish a new process group, and put the child in
          * foreground if requested
          */

         /* Put the process into the process group and give the process
          * group the terminal, if appropriate.  This has to be done both by
          * the dsh and in the individual child processes because of
          * potential race conditions.  
          * */

         p->pid = getpid();

         /* also establish child process group in child to avoid race (if parent has not done it yet). */
         set_child_pgid(j, p);
         if(fg) // if fg is set
		seize_tty(j->pgid); // assign the terminal

         /* Set the handling for job control signals back to the default. */
         signal(SIGTTOU, SIG_DFL);
}

/* Spawning a process with job control. fg is true if the 
 * newly-created process is to be placed in the foreground. 
 * (This implicitly puts the calling process in the background, 
 * so watch out for tty I/O after doing this.) pgid is -1 to 
 * create a new job, in which case the returned pid is also the 
 * pgid of the new job.  Else pgid specifies an existing job's 
 * pgid: this feature is used to start the second or 
 * subsequent processes in a pipeline.
 * */

void spawn_job(job_t *j, bool fg) 
{

	pid_t pid;
	process_t *p;

	int fd[2];  //this is the structure containing the one-way pipe... 
              // fd[0] will be the input file descriptor, and fd[1] will be the output file descriptor

  int in_pipe = 0, out_pipe = 0;
  pid_t dsh_pid = getpid();

	for(p = j->first_process; p; p = p->next) {

//	  printf("Inpipe start: %d\n", in_pipe);
	  
    // if the in_pipe file descriptor has been set in the previous loop iteration, 
    // make it the standard input for the upcoming child process
    if(in_pipe == 0) {
		in_pipe = j->mystdin;
	}
	  
    // call pipe to initialize the fd variable... 
    // this call fills it with two file descriptors describing the pipe
    if(pipe(fd)) { 
    	  perror("pipe failed");
		  exit(EXIT_FAILURE); //pipe failure
	}
//    printf("Pipes: In = %d, Out = %d\n", fd[0], fd[1]);
//    printf("The previous in_pipe: %d\n", in_pipe);

	  if(p->next) { //if there is a next process, that means this p's output should be hooked up to the pipe
		  out_pipe = fd[1];
	  }
	  else { //else, if there isn't a next process, just keep p's output as stdoutput
		  out_pipe = j->mystdout;
	  }


	  switch (pid = fork()) {

          case -1: /* fork failure */
            perror("fork");
            exit(EXIT_FAILURE);

          case 0: /* child process  */
            p->pid = getpid();	    
            new_child(j, p, fg);
            
	          /* Child-side code for new process. */

            if(p->ofile) { //if p has an output file...
            	int new_out = open(p->ofile, O_WRONLY | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO); //open the file as write only
            	dup2(new_out, STDOUT_FILENO); //hook up the stdout of the process to the file input
            	close(new_out);
            }

            if(p->ifile) { //if p has an input file
            	int new_in = open(p->ifile, O_RDONLY);  //open the file as read only
            	dup2(new_in, STDIN_FILENO); //hook up the output of the file to the stdin of the process
            	close(new_in);
            }

//            printf("Before inpipe hookup: %d\n", in_pipe);
            if(in_pipe!=0) {
            	dup2(in_pipe, STDIN_FILENO); //hook up the stdin of this process to whatever in_pipe was set as
            	close(in_pipe);
            }
            if(out_pipe!=1) {
                dup2(out_pipe, STDOUT_FILENO); //hook up the stdout of this process to whatever out_pipe was set as
                close(out_pipe);
            }

            execvp(p->argv[0], p->argv); //execute the child now that we modified the piping of the process

            perror("New child should have done an exec");
            exit(EXIT_FAILURE);  /* NOT REACHED */
            break;    /* NOT REACHED */

          default: /* parent */
            /* establish child process group */
            p->pid = pid;
            p->completed=1;
            set_child_pgid(j, p);

          /* Parent-side code for new process.  */          
          if(!fg) /* If child process runs in the background, then parent should assign the terminal back to dsh */ 
              {
                seize_tty(dsh_pid);
              }            

          // else  /* If child process is in fg, then execution should wait until the child is done */  
          //     {
          //       int status;
          //       waitpid(-1, &status, 0); // -1: Wait for any child process.
          //     }      

	  } // End of switch case

      /* Parent-side code for new job.  */          
      // If the new parent job is in fg mode, then parent should wait until the child completes execution    
//	  if(in_pipe != j->mystdin) {
//		  close(in_pipe);
//	  }


//	  printf("The in_pipe: %d\n", in_pipe);
	  if(in_pipe != 0){
//		  printf("Closing in_pipe: %d\n", in_pipe);
		  close(in_pipe);
	  }
	  if(out_pipe != 1) {
//		  printf("Closing out_pipe: %d\n", out_pipe);
		  close(out_pipe);
	  }
	  in_pipe = fd[0];
//	  printf("In pipe end: %d\n", in_pipe);


//	  close(out_pipe);

	} // End of for loop

	if(fg) {
//	  printf("Before the wait\n");
      int status;
      waitpid(-j->pgid, &status, 0); //want to wait on the child to finish if its in the foreground
//      printf("Wait status: %d\n", status);
    }

    // Between processes control has to be given to terminal, when the o/p of one process is the i/p to the next one.
//    printf("Calling process PID: %d\n", dsh_pid);
    seize_tty(dsh_pid); // assign the terminal back to dsh
//    printf("After the terminal is seized\n");
    // Well, it should take the input file descriptor for the pipe and stick it in the in_pipe variable, to be used in the next iteration (next process needs the other end of the pipe)



}

/* Sends SIGCONT signal to wake up the blocked job */
void continue_job(job_t *j) 
{
     if(kill(-j->pgid, SIGCONT) < 0)
          perror("kill(SIGCONT)");
}


/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 * it immediately.  
 */
bool builtin_cmd(job_t *last_job, int argc, char **argv) 
{
    // check whether the cmd is a built in command
        
        if (!strcmp("quit", argv[0])) {
            
            exit(EXIT_SUCCESS);
	      }

        else if (!strcmp("jobs", argv[0])) {
            
//            printf("here \n");
            job_t *job=start_job;
            job_t *j;
            process_t *p;
            int flag=1;
			for(j = job; j; j = j->next) {
				for(p=j->first_process;p;p=p->next) {
					if(p->completed!=-1) {
						flag=0;
					}
				}
				if(flag==1) {
					return true;
				}
        		fprintf(stdout, "\n#DISPLAY JOB INFO BEGIN#\njob: %ld, %s\n", (long)j->pgid, j->commandinfo);
				for(p = j->first_process; p; p = p->next) {
					fprintf(stdout,"cmd: %s\t", p->argv[0]);
					int i;
					for(i = 1; i < p->argc; i++) {
						fprintf(stdout, "%s ", p->argv[i]);
					}
					fprintf(stdout, "\n");
					fprintf(stdout, "Status: %d, Completed: %d, Stopped: %d\n", p->status, p->completed, p->stopped);
					if(p->completed==1)
					p->completed=-1;
					fprintf(stdout, "\n");
					if(p->ifile != NULL) fprintf(stdout, "Input file name: %s\n", p->ifile);
					if(p->ofile != NULL) fprintf(stdout, "Output file name: %s\n", p->ofile);
				}
			}
            	//print_job(job);
            	return true;
        }
	     
        else if (!strcmp("cd", argv[0])) {
        
         			if(argc > 1) {
        				if(chdir(argv[1])) {
        					perror("Cd command failed");
        				}
        			}
    
        	return true;
        }

        else if (!strcmp("bg", argv[0])) {

              printf("In bg builtin command function \n");

              // Case1: bg
              // Resume the execution in bg mode for the last job stopped. Note that pid is not mentioned.
              if (argc == 1)
              {
                // Need to fetch the last stopped job
                // and repeat the steps from case 2 below
              }
              // Case2: $bg 7890 
              // Resume the execution in bg mode for the job using Continue_job function and the pid
              else if (argc == 2)
              {
                pid_t bg_pid = atoi(argv[1]); // Fetching the pid
printf("lo");
                seize_tty(getpid()); // Assign terminal to dsh 

                job_t *job = NULL;
                job->pgid = bg_pid;
                continue_job(job); // Resume the suspended job 
              } 
              else
              {
                perror(" bg process failed - Invalid number of parameters");
              }

        }

        else if (!strcmp("fg", argv[0])) {

              printf("In fg builtin command function \n");

              // Case1: fg
              // Resume the execution of the last job stopped. Note that pid is not mentioned.
              if (argc == 1)
              {
                // Need to fetch the last stopped job
                // and repeat the steps from case 2 below
              }
              // Case2: $fg 7890 
              // Resume the execution of the job using Continue_job function and the pid
              else if (argc == 2)
              {
                pid_t fg_pid = atoi(argv[1]); // Fetching the pid
                seize_tty(fg_pid); // Assign terminal to this job 

                job_t *job = NULL;
                job->pgid = fg_pid;

                continue_job(job); // Resume the suspended job 
              } 
              else
              {
                perror("fg process failed - Invalid number of parameters");
              }

        }

        return false;       /* not a builtin command */
}

/* Build prompt messaage */
char* promptmsg() 
{
  int pid = getpid();
  char pid_buffer[10];
  sprintf(pid_buffer,"%d",pid);  /* Converting pid from int into char */
  char *return_value = malloc(strlen(pid_buffer)+strlen("dsh - $ ")+1); /* +1 for the zero-terminator */
  
  strcpy(return_value, "dsh - ");
  strcat(return_value, pid_buffer);
  strcat(return_value, "$ ");
  return return_value;  // Prompt looks like this 'dsh - pid $'
}

void signal_callback_handler(int signum)
{
//	printf("hey");
	job_t *j=start_job;
	process_t *p;
	for(j = start_job; j; j = j->next) {
		for(p=j->first_process;p;p=p->next) {
			if(p->completed!=1) {
				p->stopped=1;
			}
		}
 	}
	seize_tty(getpid());
	//exit(signum);
}

int main() 
{

init_dsh();

    DEBUG("Successfully initialized\n");

    while(1) {
        job_t *j = NULL;
	signal(SIGTSTP, signal_callback_handler);
        if(!(j = readcmdline(promptmsg()))) {
            if (feof(stdin)) { /* End of file (ctrl-d) */           
                fflush(stdout);
                printf("\n");
                exit(EXIT_SUCCESS);
                   }
            continue; /* NOOP; user entered return or spaces with return */
        }

        /* Only for debugging purposes to show parser output; turn off in the
         * final code */
//        if(PRINT_INFO) print_job(j);

        /* Your code goes here */
        /* You need to loop through jobs list since a command line can contain ;*/
        /* Check for built-in commands */
        /* If not built-in */
            /* If job j runs in foreground */
            /* spawn_job(j,true) */
            /* else */
            /* spawn_job(j,false) */
        int job_appended = 0;
        while(j) {

            process_t* cur_process = j->first_process;

            if(!builtin_cmd(j, cur_process->argc, cur_process->argv)) {

                //for job list
            	if(!job_appended) {
                    if(!start_job) {
                        start_job = j;
                    }
                    else {
                        while(start_job->next) {
                            start_job=start_job->next;
                        }
                        start_job->next = j;
                    }
            	}

                if(!j->bg) {
                    spawn_job(j, true);
                }
                else {
                    spawn_job(j, false);
                }

            }

            job_appended = 1;
            j = j->next;

        }
  }  // end of main while loop 

} // end of main()
