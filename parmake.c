/** 
 * @file parmake.c
 * Author: Xuefeng Zhu
*/
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "parser.h"
#include "rule.h"
#include "queue.h"


typedef struct _arg_t
{
	pthread_mutex_t *m;
	sem_t *ready_sem;
	queue_t *rest_r;
	queue_t *ready_r;
	queue_t *complete_t;
}arg_t; 

queue_t *all_rules;
queue_t *all_targets;

int isRule(char *dependency)
{
	int i;
	for (i = 0; i < queue_size(all_targets); i++)
	{
		char* target = queue_at(all_targets, i);
		if (strcmp(target, dependency) == 0)
		{
			return 1;	
		}
	}
	return 0;
}

void process_queues(queue_t *rest_r, queue_t *ready_r, queue_t *complete_t)
{
	int i = 0;
	while(i < queue_size(rest_r))
	{
		int satify = 1;
		int dependR = 0; 
		rule_t *rule = queue_at(rest_r, i);
		int j;
		
		for (j = 0; j < queue_size(rule->deps); j++)
		{
			char *dep = queue_at(rule->deps, j);

			int complete = 0;
			if (isRule(dep))
			{
				int k;
				for (k = 0; k < queue_size(complete_t); k++)
				{
					char *target = queue_at(complete_t, k);
					if (strcmp(dep, target) == 0)
					{
						dependR = 1;
						complete = 1;
						break;
					}
				}
			}
			else
			{
				if (access(dep, R_OK) == 0)
				{
					complete = 1;		
				}
				else 
				{
					fprintf(stderr,"dependency file does not exist!");
					exit(1);	
				}
			}

			if (!complete)
			{
				satify = 0;	
				break;
			}
			
		}
		
		if (satify)
		{
			if (dependR)
			{
				queue_enqueue(ready_r, rule);	
				queue_remove_at(rest_r, i);
			}
			else 
			{
				if (access(rule->target, R_OK) == -1)
				{
					queue_enqueue(ready_r, rule);	
					queue_remove_at(rest_r, i);
				}
				else 
				{
					struct stat statbuf;
					stat(rule->target, &statbuf);
					time_t rule_time = statbuf.st_mtime; 	
					
					int run = 0;
					int j;
					for (j = 0; j < queue_size(rule->deps); j++)
					{
						char *dep = queue_at(rule->deps, j);
						stat(dep, &statbuf);
						time_t dep_time = statbuf.st_mtime;
						if (dep_time > rule_time)
						{
							run = 1;
							queue_enqueue(ready_r, rule);	
							queue_remove_at(rest_r, i);
							break;
						}
					}
					
					if (!run)
					{
						queue_remove_at(rest_r, i);
						queue_enqueue(complete_t, rule->target);
						
						while(queue_size(rule->deps))
						{
							char *dep = queue_dequeue(rule->deps);
							free(dep);
						}
						while(queue_size(rule->commands))
						{
							char *command = queue_dequeue(rule->commands);
							free(command);
						}
						rule_destroy(rule);
						free(rule);
					}
				}
			}
		}
		else
		{
			i++;	
		}
	}
}

void *run_thread(void *args)
{
	arg_t *run_args = (arg_t *)args;
	
	while(1)
	{
		if (queue_size(run_args->ready_r) == 0 && 
			queue_size(run_args->rest_r) == 0)
		{
			return NULL;	
		}
		sem_wait(run_args->ready_sem);
		while(queue_size(run_args->ready_r) == 0)
		{
			if (queue_size(run_args->ready_r) == 0 && 
				queue_size(run_args->rest_r) == 0)
			{
				sem_post(run_args->ready_sem);
				return NULL;	
			}
			sem_wait(run_args->ready_sem);
		}
		rule_t *rule;
		pthread_mutex_lock(run_args->m);
		rule = queue_dequeue(run_args->ready_r);
		pthread_mutex_unlock(run_args->m);

		while(queue_size(rule->commands))
		{
			char *command = queue_dequeue(rule->commands);
			if (system(command) != 0)
			{
				exit(1);	
			}
			free(command);
		}

		char *temp = rule->target;
		while(queue_size(rule->deps))
		{
			char *dep = queue_dequeue(rule->deps);
			free(dep);
		}
		rule_destroy(rule);
		free(rule);
		
		pthread_mutex_lock(run_args->m);
		int pSize = queue_size(run_args->ready_r);
		queue_enqueue(run_args->complete_t, temp);
		process_queues(run_args->rest_r, run_args->ready_r, run_args->complete_t);
		int cSize = queue_size(run_args->ready_r);
		pthread_mutex_unlock(run_args->m);
		int i;
		for(i = 0; i < cSize - pSize; i++)
		{
			sem_post(run_args->ready_sem);
		}
		sem_post(run_args->ready_sem);
	}
}

void parsed_new_targer(char *target)
{
	char *temp = malloc(strlen(target)+1);
	strcpy(temp, target);

	rule_t *rule = malloc(sizeof(rule_t));
	rule_init(rule);
	rule->target = temp;
	queue_enqueue(all_rules, rule);
	queue_enqueue(all_targets, temp);
}

void parsed_new_dependency(char *target, char* dependency)
{
	char *temp = malloc(strlen(dependency)+1);
	strcpy(temp, dependency);

	int size = queue_size(all_rules);
	rule_t *rule = queue_at(all_rules, size-1);
	queue_enqueue(rule->deps, temp);	
}

void parsed_new_command(char *target, char* command)
{
	char *temp = malloc(strlen(command)+1);
	strcpy(temp, command);

	int size = queue_size(all_rules);
	rule_t *rule = queue_at(all_rules, size-1);
	
	queue_enqueue(rule->commands, temp);	
}
/**
 * Entry point to parmake.
 */
int main(int argc, char **argv)
{
	int opt;
	int count = 0;
	char *fname = NULL; 
	int nthreads = 1; 
	char **targets = NULL;

	while ((opt = getopt(argc, argv, "f:j:")) != -1)
	{
		switch (opt)
		{
			case 'f':
				count++;
				fname = optarg;
				break;
			case 'j':
				count++;
				nthreads = atoi(optarg);
				break;
		}
	}

	if (fname == NULL)
	{
		//check if ./makefile exists
		if (access("./makefile", R_OK) == 0)
		{
			fname = "./makefile";
		}
		else 
		{
			//check if ./Makefile exists
			if (access("./Makefile", R_OK) == 0)
			{
				fname = "./Makefile";	
			}
			else 
			{
				return -1;	
			}
		}
	}
	else 
	{
		//check if file exists
		if (access(fname, R_OK) == -1)
		{
			return -1;	
		}
	}

	int tlength = argc - 2 * count;
	if (tlength > 1)
	{
		int temp = 1 + 2 * count;
		int i;
		targets = malloc(tlength * sizeof(char*));
		for (i = 0; i < tlength-1; i++)
		{
			targets[i] = argv[temp + i];	
		}

		targets[i] = NULL;
	}

	//part 2 paser makefile 
	all_rules = malloc(sizeof(queue_t));
	all_targets = malloc(sizeof(queue_t));
	queue_init(all_rules);
	queue_init(all_targets);

	parser_parse_makefile(fname, targets, parsed_new_targer, parsed_new_dependency, parsed_new_command);

	//part 3 run rules
	queue_t *ready_r = malloc(sizeof(queue_t));
	queue_t *complete_t = malloc(sizeof(queue_t));
	queue_init(ready_r);
	queue_init(complete_t);
	process_queues(all_rules, ready_r, complete_t);

	while (queue_size(ready_r) == 0)
	{
		process_queues(all_rules, ready_r, complete_t);	
	}

	//part 4 parallel
	
	pthread_mutex_t m;
	pthread_mutex_init(&m, NULL);

	sem_t ready_sem;
	sem_init(&ready_sem, 0, queue_size(ready_r));

	arg_t *run_args = malloc(sizeof(arg_t));
	run_args->m = &m;
	run_args->ready_sem = &ready_sem;
	run_args->rest_r = all_rules;
	run_args->ready_r = ready_r;
	run_args->complete_t =  complete_t;

	pthread_t threads[nthreads];
	int i;

	for (i = 0; i < nthreads; i++)
	{
		pthread_create(&threads[i], NULL, run_thread, (void *)run_args);	
	}

	for (i = 0; i < nthreads; i++)
	{
		pthread_join(threads[i], NULL);	
	}

	//free memory
	while(queue_size(complete_t))
	{
		char *target = queue_dequeue(complete_t);
		free(target);
	}

	queue_destroy(all_rules);
	queue_destroy(all_targets);
	queue_destroy(ready_r);
	queue_destroy(complete_t);
	free(targets);
	free(all_rules);
	free(all_targets);
	free(ready_r);
	free(complete_t);
	free(run_args);
	return 0; 
}
