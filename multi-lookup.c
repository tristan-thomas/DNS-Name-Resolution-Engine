#include "util.h"
#include "multi-lookup.h"
clock_t clock(void);

/* The size of shared memory object */
#define SIZE 1024

struct sharedVariables{
    void* ptr;
    sem_t sem1, dfPool, resLog;
    int index, filesProcessed, filesToProcess, wsRequested, wsResolved;
    char fileNames[10][11];
    char resolverLog[12];
    char serviced[13];
    int lengths[30];
};

void requester(struct sharedVariables* args){
  int jobToBeDone = 1;
  int numFiles = 0;
  while(jobToBeDone){
    /* Wait for file pool */
    sem_wait(&args->dfPool);
    if(args->filesProcessed<args->filesToProcess){
      /* Grab file */
      int fileNum = args->filesProcessed;
      char filePath[strlen(args->fileNames[fileNum])+strlen("input/")];
      strcpy(filePath,"input/");
      strcat(filePath, args->fileNames[fileNum]);

      args->filesProcessed++;
      /* Signal file pool */
      sem_post(&args->dfPool);
      /* Process file */
      FILE* fp;
      if((fp = fopen(filePath, "r"))){
        numFiles++;
        char websiteName[20];
        while((fgets(websiteName, 20, fp))){
          websiteName[strlen(websiteName)-1] = '\0';
          // printf("Website grabbed: %s\n",websiteName);
          /* Wait for queue to not be full */
          while(args->index >= 29){
            sleep(0.1);
          }
          /* Wait */
          sem_wait(&args->sem1);

          unsigned int len = sprintf(args->ptr,"%s",websiteName);
          args->ptr += len;
          args->lengths[args->index++]=len;
          args->wsRequested++;
          /* Signal */
          sem_post(&args->sem1);
        }
        if(fclose(fp)){
          printf("ERR: File: '%s' failed to close.\n",args->fileNames[(args->filesProcessed)-1]);
        }
      }
      else{
        printf("ERR: Could not open file at:%s\n",filePath);
      }
    }
    else{
      /* Finished work, set bool to false */
      jobToBeDone = 0;
      /* Signal file pool */
      sem_post(&args->dfPool);
    }
  }
  FILE* serviced;
  sem_wait(&args->dfPool);
  if((serviced = fopen(args->serviced, "a"))){
    fprintf(serviced, "Thread <%li> processed [%d] files.\n",syscall(SYS_gettid),numFiles);
    if(fclose(serviced)){
      printf("ERR: File: '%s' failed to close.\n",args->serviced);
    }
    sem_post(&args->dfPool);
  }
  else{
    printf("FAILED TO OPEN SERVICED FILE\n");
    sem_post(&args->dfPool);
  }
}

void resolver(struct sharedVariables* args){
  char IPstr[INET6_ADDRSTRLEN];
  char websiteName[20];
  int length=0;
  FILE* fp=NULL;
  memset(IPstr,0,INET6_ADDRSTRLEN);
  memset(websiteName,0,20);
  /* If resover threads beat the requester threads to their work
     loop, wait until the requesters have begun their work, otherwise
     the resolver work loop will be surpassed. */
  while(args->wsRequested==0)sleep(0.001);
  while(args->wsResolved<args->wsRequested || args->filesProcessed<args->filesToProcess){
    /* Wait */
    sem_wait(&args->sem1);

    /* Check if buff is empty */
    if(args->index>0){
      args->index--;
      length = args->lengths[args->index];
      args->ptr -= length;
      memcpy(websiteName,args->ptr,length);
      args->wsResolved++;
      //printf("looking up websiteName:%s\n",websiteName);
      /* Signal  buffer mutex */
      sem_post(&args->sem1);
      if(!dnslookup(websiteName,IPstr,INET6_ADDRSTRLEN)){
        printf("DNS Lookup Succesful: %s, %s\n",websiteName,IPstr);
        /* Wait for resolver log mutex */
        sem_wait(&args->resLog);
        if(!(fp = fopen(args->resolverLog, "a"))){
          printf("ERR: Could not open file: '%s'\n",args->resolverLog);
        }
        else{
          fprintf(fp, "%s,%s\n",websiteName,IPstr);
          if(fclose(fp)){
            printf("ERR: File: '%s' failed to close.\n", args->resolverLog);
          }
        }
        /* Signal resolver log mutex */
        sem_post(&args->resLog);
      }
      else{
        printf("ERR: DNS Lookup Failed: %s\n",websiteName);
        /* Wait for resolver log mutex */
        sem_wait(&args->resLog);
        if(!(fp = fopen(args->resolverLog, "a"))){
          printf("ERR: Could not open file: '%s'\n",args->resolverLog);
        }
        else{
          fprintf(fp, "%s,\n",websiteName);
          if(fclose(fp)){
            printf("ERR: File: '%s' failed to close.\n", args->resolverLog);
          }
        }
        /* Signal resolver log mutex */
        sem_post(&args->resLog);
      }
      memset(websiteName,0,length);
    }
    /* If buffer is empty, release mutex */
    else{
      sem_post(&args->sem1);
    }
    /*
    printf("Websites requested: %d | ",args->wsRequested);
    printf("Websites resolved: %d\n",args->wsResolved);
    */
  }
}


int main(int argc, char const *argv[]) {
  // start clock
	clock_t begin, finish;
	begin = clock();
  if(argc>5){
    sem_t sem1, dfPool, resLog;
    struct sharedVariables VARS;
    for(int i=0; i<30; i++){
      VARS.lengths[i]=0;
    }
    int pshared = 0;
    int numRequesters = atoi(argv[1]);
    int numResolvers = atoi(argv[2]);
    const char* requesterLog = argv[3];
    const char* resolverLog = argv[4];
    int numDataFiles = argc-5;
    /* Arrays to hold threads */
    pthread_t requesters[numRequesters];
    pthread_t resolvers[numResolvers];
    /* Name of the shared memory object */
    const char *name = "/SHARED_BUFF";
    /* Create the shared memory object */
    int shm_fd;
    if((shm_fd = shm_open(name, O_CREAT | O_RDWR, 0666))){
      //printf("Shared memory created succesfully.\n");
      /* Create a binary semaphore for the shared buffer */
      if(!(sem_init(&sem1, pshared, 1))){
        //printf("Bounded Buffer Semaphore created succesfully.\n");
        VARS.sem1 = sem1;
      }
      else{
        printf("ERR: Failed to create Bounder Buffer semaphore.\n");
      }
      /* Create a binary semaphore for the data files */
      if(!(sem_init(&dfPool, pshared, 1))){
        //printf("Data Files Semaphore created succesfully.\n");
        VARS.dfPool = dfPool;
      }
      else{
        printf("ERR: Failed to create Data Files semaphore.\n");
      }
      /* Create a binary semaphore for the resolverLog */
      if(!(sem_init(&resLog, pshared, 1))){
        //printf("Res Log Semaphore created succesfully.\n");
        VARS.resLog = resLog;
      }
      else{
        printf("ERR: Failed to create Res Log semaphore.\n");
      }
    }
    else{
      printf("ERR: Shared memory creation failed.\n");
    }
    /* Configure the size of the shared memory object */
    if((ftruncate(shm_fd, SIZE)<0)){
      printf("ERR: ftruncate failed.\n");
    }
    /* Configure Shared Variables Struct */
    VARS.index = 0;
    VARS.filesProcessed = 0;
    VARS.filesToProcess = numDataFiles;
    VARS.wsRequested = 0;
    VARS.wsResolved = 0;
    /* Memory map the shared memory object */
    VARS.ptr = mmap(0, SIZE, PROT_WRITE|PROT_READ, MAP_SHARED, shm_fd, 0);
    /* Change struct later to allow for more VARS.fileNames */
    for(int i=0; i<numDataFiles; i++){
      strcpy(VARS.fileNames[i],argv[5+i]);
      strcat(VARS.fileNames[i],"\0");
      //printf("fileNames[%d]: %s\n",i,VARS.fileNames[i]);
    }
    for(int i=numDataFiles;i<10-numDataFiles;i++){
      memset(VARS.fileNames[1],0,11);
    }
    strcpy(VARS.resolverLog,resolverLog);
    strcpy(VARS.serviced,requesterLog);

    /* Creation/Joining of all Threads */
      /* Create all resolver threads */
      for(int i=0; i<numResolvers; i++){
        pthread_create(&resolvers[i],NULL,&resolver,&VARS);
      }
      printf("All resolver threads created.\n");

      /* Create all requester threads */
      for(int i=0; i<numRequesters; i++){
        pthread_create(&requesters[i],NULL,&requester,&VARS);
      }
      printf("All requester threads created.\n");

      /* Join all resolver threads */
      for(int i=0; i<numResolvers; i++){
        pthread_join(resolvers[i], NULL);
      }
      printf("All resolver threads joined.\n");

      /* Join all requester threads */
      for(int i=0; i<numRequesters; i++){
        pthread_join(requesters[i], NULL);
      }
      printf("All requester threads joined.\n");
    /* END: Creation/Joining of all Threads */

    /* Free Shared Memory */
    if(shm_unlink(name)){
      printf("ERR: Shared memory failed to delete.\n");
    }
    else{
      //printf("Shared memory deleted succesfully.\n");
      /* Destroy Semaphore for shared buffer */
      if((sem_destroy(&sem1))){
        printf("ERR: Failed to destroy Bounded Buffer semaphore...\n");
      }
      else{
        //printf("Bounded Buffer Semaphore destroyed succesfully!\n");
      }
      /* Destroy Semaphore for Data File pool */
      if((sem_destroy(&dfPool))){
        printf("ERR: Failed to destroy Data File pool semaphore...\n");
      }
      else{
        //printf("Data File pool Semaphore destroyed succesfully!\n");
      }
      /* Destroy Semaphore for Data File pool */
      if((sem_destroy(&resLog))){
        printf("ERR: Failed to destroy Res Log semaphore...\n");
      }
      else{
        //printf("Res Log Semaphore destroyed succesfully!\n");
      }
    }
  }
  else{
    printf("ERR: Invalid number of command line arguments.\n");
  }
  printf("Finished. Program terminating...\n");
  // end time
	finish = clock();
	printf("Time elapsed = [%li] ticks.\n", finish-begin);
  return 0;
}
