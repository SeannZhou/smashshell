#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h> 
#include <sys/types.h>
#include <sys/wait.h>
#include <execinfo.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h> // for open

// Job entry list
#define MAXJOBS 10   			// Program can hold up to 10 processes
struct job {         			// Struct for each job     
    pid_t pid;              	// Job pid
    int job_num;           
    char command[30];			// Command     
    int process_state;          // 0: nothing	1: fg 	2: bg 	3: stopped
    int retval;					// Return value of command
};
struct job jobs[MAXJOBS]; 			// List of jobs
static struct job lastJobComplete; 	// Last job to be completed
static int amount_of_jobs = 0;		// Current amount of jobs

// Redirection functions
void swapstdout(int opfd, int stdoutput);
void swapstderr(int errfd, int stderror);
void swapstdin(int infd, int stdinput);

// Echo function
int echo(char **parameters, int retval);

// Job list function
void startjobs();
void listjobs();
int maxjobnum(); 
int addjob(pid_t pid, int process_state, char *command);
int deletejob(pid_t pid);
void clearjob(struct job *oldjob);
struct job *getjobpid(pid_t pid);
struct job *getjobjobnum(int jobnum);
int bgcont(int jobnum);
int fgcont(int jobnum);

// Signal functions
typedef void sigfunc(int);
sigfunc *Signal(int signum, sigfunc *handler);
void sigchld_handler(int sig);
void sigkill_handler(int sig);
void sigtstp_handler(int sig);
void sigquit_handler(int sig);

// Reading commandline
void read_smash_line(char **line_content, int *outfile, int *errfile, int *infile);
void free_smash_args(char **args);

// Helper
pid_t Fork(void);

// If d flag passed print to stderr
static int opt_d = 0;
// To save whether command is to be ran in the background
static int runbg = 0;				

int main(int argc, char **argv, char **envp) {
	char opt;
  	while ((opt = getopt(argc, argv, "d")) != -1) {
    	switch (opt) {
    		case 'd':
      			opt_d = 1;
      			break;
      	}
  	}

  	// Creating signals
  	Signal(SIGQUIT, sigquit_handler);	// Reap any child that quit
  	Signal(SIGCHLD, sigchld_handler);  	// terminated or stopped child 
    Signal(SIGINT, sigkill_handler); 	// ctrl-c
  	Signal(SIGTSTP, sigtstp_handler);  	// ctrl-z 

  	// Initializing jobs array
  	startjobs();
  	clearjob(&lastJobComplete);

  	// Parsing tools
  	char command[256] = "";		// Command Parsed
  	char *parameters[100];		// Command line args
  	char bincommand[100];		// Command with to hold /bin/ before concat
  	
  	// Process tools
  	int retval = 0;				// Return value
  	int nonforkCommand;			// Check if forked command
  	pid_t forkpid;

  	// Redirection file description
  	int opfd = 0;		// o/p file descriptor
  	int stdoutput = 0;	// Hold stdout when switching
  	int errfd = 0;		// error file descriptor
  	int stderror = 0;	// Hold stderr when switching
  	int infd = 0;		// i/p file descriptor
  	int stdinput = 0;	// Hold stdin when switching

  	while (1) {
  		runbg = 0;	// Run in the background
  		// Redirection file descriptors
  		opfd = -1;
  		errfd = -1;
  		infd = -1;

  		// SMASH prompt 
  		printf("smash> ");
  		read_smash_line(parameters, &opfd, &errfd, &infd);
		// Setting command (check if empty)
		strcpy(command, parameters[0]);		// First element is command
  		nonforkCommand = 0;

  		if (opt_d == 1) {
  			printf("\nPrinting parameters:\n");
			// Print tokens of line
			for (int i = 0; parameters[i] != NULL; i++) {
				printf("elem %d: |%s|\n", i, parameters[i]);	
			}	
  		}

	  	// Redirect output
	  	if (opfd != -1) {
	  		stdoutput = dup(1);
	  		dup2(opfd, 1);
	  	}
	  	// Redirect error
	  	if (errfd != -1) {
	  		stderror = dup(2);
	  		dup2(errfd, 2);
	  	}
	  	// Redirect input
	  	if (infd != -1) {
	  		stdinput = dup(0);
	  		dup2(infd, 0);
	  	}


  		if (strcmp(command, "") == 0) {
  			// Debug print
	  		if (opt_d == 1) {
	  			printf("DEBUG: Nothing passed in\n");
	  		}

			// Reset file descriptors if swapped
			if (opfd > 0) {
				swapstdout(opfd, stdoutput);
			}
			if (errfd > 0) {
				swapstdout(errfd, stderror);
			}
			if (infd > 0) {
				swapstdout(infd, stdinput);
			}
  			free_smash_args(parameters);
  			continue;
  		}

  		// Exit program
  		if (strcmp(command, "exit") == 0 && parameters[1] == naNULL) {
  			// Debug print
	  		if (opt_d == 1) {
	  			printf("DEBUG: Exiting...\n");
	  		}

  			// Reset file descriptors if swapped
			if (opfd > 0) {
				swapstdout(opfd, stdoutput);
			}
			if (errfd > 0) {
				swapstdout(errfd, stderror);
			}
			if (infd > 0) {
				swapstdout(infd, stdinput);
			}
  			free_smash_args(parameters);
  			break;
  		} 

  		// Debug print
  		if (opt_d == 1) {
  			printf("RUNNING: %s\n", command);
  		}

  		// Flush any output
  		fflush( stdout );
		fflush( stderr );

  		// bin command concatation
  		strcpy(bincommand, "/bin/");
  		strcat(bincommand, command);
		// Commands that don't need to be forked: cd, pwd, ls
			// ls command
  		if (strcmp(command, "ls") == 0) {
  			runbg = 0;	// Don't run ls in the bg	
  		}  

  			// Set environment variables
  		if (strchr(command, '=') != NULL) {
   			char *variable = strtok(command, "=");
  			char *value = strtok(NULL, "=");
  			retval = setenv(variable, value, 0);

  			// pwd command
  		} else if (strcmp(command, "pwd") == 0) {
			char pwd[1024] = "empty"; 
		    getcwd(pwd, sizeof(pwd)); 
		    // If cwd contents are still "empty"
		    if (strcmp(pwd, "empty") == 0) {
		    	retval = 1;
		    } else {
		    	retval = 0;
		    }
		    printf("%s\n", pwd);

		    // cd command
		} else if (strcmp(command, "cd") == 0) {
			retval = chdir(parameters[1]); 
			if (retval == -1) {
				printf("Failed to change dir...\n");
			}

			// jobs command
		} else if (strcmp(command, "jobs") == 0) {
			listjobs();
			retval = 0;

			// bg command
		} else if (strcmp(command, "bg") == 0) {	// Send SIGCONT to job in the background
			retval = bgcont(atoi(parameters[1]));
			if (retval == -1) {
				printf("Job id does not exist\n");
			}

			// fg command
		} else if (strcmp(command, "fg") == 0) {	// Send SIGCONT to job in the background
			retval = fgcont(atoi(parameters[1]));
			if (retval == -1) {
				printf("Job id does not exist\n");
			}

			// kill command
		} else if (strcmp(command, "kill") == 0) {		// Send SIGCONT to job in the background
			int signalnum = atoi(parameters[1] + 1);	// Get signal number passed kill -N
			int jobnum = atoi(parameters[2]);

			struct job *jobreceived = getjobjobnum(jobnum);   	
			if (jobreceived == NULL) {
				printf("Job does not exist...\n");
				retval = 1;
			} else {
				if (kill(jobreceived->pid, signalnum) == -1) {
					printf("Sending signal failed...\n");
					retval = 1;
				}
				retval = 0;
			}

			// Execute echo command 
		} else if (strcmp(command, "echo") == 0) {
	  		retval = echo(parameters, retval);
	  		if (retval == 1) {
	  			printf("Nothing was passed to echo...\n");
	  		}

			// Execute any bin commands 
		} else {
			// Forking smash commands	
			nonforkCommand = 1;
			forkpid = Fork();
  				// Forking failed
	  		if (forkpid == 0) {

	  			// Only run file if it exists
	  			struct stat stats;
				stat(command, &stats);
	  			if (S_ISREG(stats.st_mode) == 1) {
	  				execv(command, parameters);	
	  			} else {	  				
	  				execv(bincommand, parameters);
	  			}

	  			// Exit fail status in child process if execution failed
	  			printf("\nCommand failed to execute...\n");	
	  			free_smash_args(parameters);
  				exit(1);
	  			
	  			// Parent process waiting
	  		} else {
	  				// printf("CURRENT PROCESS ID: %d\n", forkpid);

	  			// If & passed don't wait
				if (runbg == 1) {
					addjob(forkpid, 2, command);
					// 2nd Parent process
					free_smash_args(parameters);
					continue;	// Parent process that will continue promting

				} else {
					// Run in fg
					addjob(forkpid, 1, command);
				}

				// Debug print
		  		if (opt_d == 1) {
		  			printf("DEBUG: Parent process waiting\n");
		  		}

				// Pause parent for fg process
				pause();
	  		}
	  	}

	  	// Free allocated args
		free_smash_args(parameters); 
		if (opt_d == 1 && nonforkCommand == 0) {	// Debug print
			printf("ENDED: %s (ret=%d)\n", command, retval);
		}

		// Flush any output
		fflush( stdout );
		fflush( stderr );

		// Reset file descriptors if swapped
		if (opfd > 0) {
			swapstdout(opfd, stdoutput);
		}
		if (errfd > 0) {
			swapstdout(errfd, stderror);
		}
		if (infd > 0) {
			swapstdout(infd, stdinput);
		}

  	} // Inside while loop

	exit(0);
	return 0;
}

// Close file descriptors and swap stdout, stderr, stdin
void swapstdout(int opfd, int stdoutput) {
	dup2(stdoutput, 1);	// Switch stdout back in place
	close(opfd);
	close(stdoutput);
	// Debug print
	if (opt_d == 1) {
		printf("DEBUG: Closing STDOUT\n");
	}
}

void swapstderror(int errfd, int stderror) {
	dup2(stderror, 2);	// Switch stdout back in place
	close(errfd);
	close(stderror);
	// Debug print
	if (opt_d == 1) {
		printf("DEBUG: Closing STDERR\n");
	}
}

void swapstdin(int infd, int stdinput) {
	dup2(stdinput, 0);	// Switch stdout back in place
	close(infd);
	close(stdinput);
	// Debug print
	if (opt_d == 1) {
		printf("DEBUG: Closing STDIN\n");
	}
}

// Echo function
int echo(char **parameters, int retval) {
	// Nothing was passed
	if (parameters[1] == NULL) {
		return 1;
	}

		// Echo last return value
	if (strcmp(parameters[1], "$?") == 0) {
		printf("%d\n", retval);
		return 0;

		// Echo environvar or passed value	
	} else {
		// Loop through passed arguments
		// char dollersign;
		char *variable;
		char *value;
		for (int i = 1; parameters[i] != NULL; i++) {		// Start at 1 to skip commmand
				// If passing environment variable
			if (*(parameters[i]) == '$') {	
				variable = strtok(parameters[i], "$");
				value = getenv(variable);
				// printf("Value: %s\n", value);
				if (value == NULL) {
					// Debug print
			  		if (opt_d == 1) {
			  			printf("DEBUG: $%s does not exist... ", value);
			  		}
					
				} else {
					printf("%s ", value);	
				}

				// Print value back if not environment variable
			} else {
				printf("%s ", parameters[i]);
			}
		}
		printf("\n");
	}

	return 0;
}

// Job list functions
// initializing job list
void startjobs() {
    // Debug print
	if (opt_d == 1) {
		printf("DEBUG: Job list initializing...\n");
	}

    for (int i = 0; i < MAXJOBS; i++) {
    	clearjob(&jobs[i]);	
    }
}

// jobs command: list current jobs
void listjobs() {
	// If a queued bg process has completed
	if (lastJobComplete.pid != 0) {
		printf("[%d] Exit %d\t%s\n", lastJobComplete.job_num, lastJobComplete.retval, lastJobComplete.command);
		clearjob(&lastJobComplete);
	}
	if (amount_of_jobs == 0) {
		printf("There are currently no jobs running or stopped...\n");
		return;
	}

	printf("Current job(s):\n");
    for (int i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid != 0) {
		    printf("[%d] (%d) ", jobs[i].job_num, jobs[i].pid);
		    switch (jobs[i].process_state) {
				case 1: 
				    printf("Foreground\t");
				    break;
				case 2: 
				    printf("Running\t");
				    break;
				case 3: 
				    printf("Stopped\t");
				    break;
			    default:
				    printf("listjobs: Internal error: job[%d].state=%d ", 
					   i, jobs[i].process_state);
			    }
			    printf("%s\n", jobs[i].command); 
		}
    }
}

// Return the next job id to be saved
int maxjobnum() {
    int topjobnum = 0;
    for (int i = 0; i < MAXJOBS; i++) {
    	if (jobs[i].job_num > topjobnum) {
    		topjobnum = jobs[i].job_num;		
    	}
    }
    return topjobnum;
}

// Add to job list
int addjob(pid_t pid, int state, char *command) {
    // Bad PIDs
    if (pid < 1) {	
		return 0;	
    }

    // Program cannot exceed max jobs
    if (amount_of_jobs >= MAXJOBS) {
    	printf("Tried to create too many jobs\n");
    	return 1;
    }

    for (int i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == 0) {
		    jobs[i].pid = pid;
		    jobs[i].process_state = state;
		    jobs[i].job_num = ++amount_of_jobs;
		    strcpy(jobs[i].command, command);

		    // Too many jobs
		    if (amount_of_jobs >= MAXJOBS) {
				amount_of_jobs = 1;
		    }
		    return 1;
		}
    }
    
    return 0;
}

// Deleting jobs
int deletejob(pid_t pid) {
	// Bad PIDs
    if (pid < 1) {
    	return 0;	
    }
    for (int i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == pid) {
		    clearjob(&jobs[i]);
		    amount_of_jobs = maxjobnum();
		    return 1;
		}
    }
    return 0;
}

// For reseting job values
void clearjob(struct job *oldjob) {
    oldjob->pid = 0;
    oldjob->job_num = 0;
    oldjob->process_state = 0;
    strcpy(oldjob->command, "");
}

// Gets a job by pid
struct job *getjobpid(pid_t pid) {
	// Bad PIDs
    if (pid < 1) {
    	return NULL;	
    }
    for (int i = 0; i < MAXJOBS; i++) {
    	if (jobs[i].pid == pid) {
	    	return &jobs[i];	
    	}
    }
    return NULL;
}

// Get job by job id
struct job *getjobjobnum(int jobnum) {
	// Bad PIDs
    if (jobnum < 1) {
    	return NULL;	
    }	
    for (int i = 0; i < MAXJOBS; i++) {
    	if (jobs[i].job_num == jobnum) {
    		return &jobs[i];		
    	}
    }
    return NULL;
}


int bgcont(int jobnum) {
	if (jobnum == 0) {
		return -1;		
	}
	for (int i = 0; i < MAXJOBS; i++) {
		if (jobs[i].job_num == jobnum) {
			jobs[i].process_state = 2; 	// Switch to running
			kill(jobs[i].pid, SIGCONT);
			return 0;
		}
	}
	return -1;
}

int fgcont(int jobnum) {
	if (jobnum == 0) {
		return -1;		
	}
	for (int i = 0; i < MAXJOBS; i++) {
		if (jobs[i].job_num == jobnum) {
			jobs[i].process_state = 1;	// Switch to foreground
			kill(jobs[i].pid, SIGCONT);	
			pause();
			
			return 0;
		}
	}
	return -1;
}

// Wrapper function for signal
sigfunc *Signal(int signum, sigfunc *handler)  {
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0) {
    	fprintf(stderr, "Error: signal error %d\n", errno);
    }
	
    return (old_action.sa_handler);
}

// Sigchild handler to catch retval
void sigchld_handler (int sig) {
  	int pid, status, serrno, returnval;
  	serrno = errno;
  	while (1) {
      	pid = waitpid (WAIT_ANY, &status, WNOHANG);
      	returnval = WEXITSTATUS(status);
	  	
      	if (pid <= 0) {
          	break;
        } else {
        	struct job *jobreceived = getjobpid(pid);   	
        	jobreceived->retval = returnval;
        	if (opt_d == 1) {	// Debug print
  				printf("ENDED: %s (ret=%d)\n", jobreceived->command, returnval);
  			}

  			if (jobreceived->process_state == 2) {
  				lastJobComplete = *jobreceived;  			
  			}
        	deletejob(pid);

        }
    }
  	errno = serrno;
}

// Reap any children 
void sigquit_handler(int sig)  {
	if (opt_d == 1) {	// Debug print
		printf("ENDED: (ret=%d)\n", 1);
	}
    exit(1);
}

void sigkill_handler(int sig) {
	pid_t fgpid;
	for (int i = 0; i < MAXJOBS; i++) {
		// Check for fg process
		if (jobs[i].process_state == 1) {
			fgpid = jobs[i].pid;	
		} else if (jobs[i].process_state == 2) {
			kill(jobs[i].pid, SIGCONT);
		}
	}

	struct job *jd = getjobpid(fgpid);
	if (jd == NULL) {
		return;
	}
	if (fgpid != 0) {
		printf("Job [%d] (%d) terminated by signal: Interrupt\n", jd->job_num, fgpid);
		kill(fgpid, SIGKILL);   //terminate process
	}

    return;
}

// Stop the fg process when ctrl-z is hit
void sigtstp_handler(int sig) {
    pid_t fgpid = -1;
	int i;
	for (i = 0; i < MAXJOBS; i++) {
		// Check for fg process
		if (jobs[i].process_state == 1) {
			fgpid = jobs[i].pid;	
		} else if (jobs[i].process_state == 2) {
			kill(jobs[i].pid, SIGCONT);
		}
	}

	// If there's no fg process running
	if (fgpid == -1) {
		return;
	}

	struct job *jd = getjobpid(fgpid);
	if(fgpid != 0) {
		printf("Job [%d] (%d) stopped by signal: Stopped\n", jd->job_num, fgpid);
		kill(fgpid, SIGSTOP);  //terminate process
		jd->process_state = 3; //change status to stopped
	}        

    return;
}

// Helper functions
// Reads SMASH line command contents
void read_smash_line(char **line_content, int *outfile, int *errfile, int *infile) {
	char smash_line[256];			// Command line str
  	char *line_array[100];			// Command line string tokens
  	int charcounter = 0;
	char character = '0';
	while (character != '\n') {
		character = fgetc(stdin);	
		smash_line[charcounter++] = character;
	}
	smash_line[charcounter] = '\0';	// Give an endline

	char *token = strtok(smash_line, " \t\r\n\a");
	// Ignore if line is empty of comment
	if (token == NULL || *token == '#') {
		line_content[0] = strdup("");
		line_content[1] = NULL;
		return;
	}

	char redirection1;		// First 2 characters are checked if they are for redirections
	char redirection2;
	char *file;				// Holds redirected file name
	char *leftover;			// Hold the leftover of redirection strtok
	int tokencounter = 0;
	while (token != NULL) { 	
		redirection1 = *token;
		redirection2 = *(token + 1);

		if (redirection1 == '>') {
			file = strtok_r(token, ">", &leftover);
			*outfile = creat(file, 0777);

			token = strtok(NULL, " \t\r\n\a"); 
			continue;

			// redirect stderr
		} else if (redirection1 == '2' && redirection2 == '>') {
			file = strtok_r(token, "2>", &leftover);
			*errfile = creat(file, 0777);

			token = strtok(NULL, " \t\r\n\a"); 
			continue;

			// redirect stdin
		} else if (redirection1 == '<') {
			file = strtok_r(token, "<", &leftover);
			*infile = open(file, O_RDONLY);

			token = strtok(NULL, " \t\r\n\a"); 
			continue;
		}
		// Break if we hit bg symbol
		if (strcmp(token, "&") == 0) {
			runbg = 1;
			break;
		}

		line_array[tokencounter] = strdup(token);
		if (line_array[tokencounter] == NULL) {			// If strdup failed to alloc memory
			fprintf(stderr, "Error: strdup ran out of mem %d\n", errno);
			exit(ENOMEM);
		}
		tokencounter++;		
		token = strtok(NULL, " \t\r\n\a"); 
	} 

	int i;
	for (i = 0; i < tokencounter; i++) {
		line_content[i] = line_array[i];
	}
	line_content[i] = NULL;	// Last element will be NULL
}

// Frees allocated memory of parameters
void free_smash_args(char **args) {
	// Free the dynamically saved arguments
	int i;
	for (i = 0; args[i] != NULL; i++) {
		free(args[i]);	
	}
}

pid_t Fork(void) {
	pid_t pid;
	
	if((pid = fork()) < 0) {
		fprintf(stderr, "Error: fork error %d\n", errno);
  	}
  	
  return pid;
}
