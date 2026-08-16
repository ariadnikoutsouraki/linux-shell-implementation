#include<stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include "unistd.h"
#include <stdlib.h>
#include <sys/wait.h>
#define MAX_INPUT 1024
#define MAX_ARGS 64
int main()
{
    while(1)
    {
        char input[MAX_INPUT];
        char*args[MAX_ARGS];

        printf("tiny_shell> ");//Εμφάνιση prompt
        fflush(stdout);

        if(fgets(input, MAX_INPUT , stdin) == NULL)
        {
            if (feof(stdin)) {
                printf("\nExiting shell on EOF.\n");
                break; // Τερματίζει στο EOF
            }
            perror("fgets,failed");
            break;//Τερματίζει σε άλλο σφάλμα
            
        }
        // Έλεγχος για κενή είσοδο
        if (input[0] == '\0') 
        {
            continue;
        }

        input[strcspn(input,"\n")]='\0'; //βάζει τον χαρακτήρα τερματισμού στη θέση του \n
        if(strcmp(input,"exit")==0)
        {
            printf("exiting tinyshell\n");//συγκρίνει αν ελαβε exit για να τερματήσει
            break;
        }

        //char* token=strtok(input," \t\n");
        //int i=0;
        //while (token!=NULL && i<MAX_ARGS)
        //{
        //    args[i]=token;//πίνακας που περιέχει κάθε λέξη
        //    token=strtok(NULL," \t\n");//πρώτο όρισμα το NULL για να θυμάται που σταμάτησε 
        //    i++;//επανάληψη για να λάβω όλες τις λέξεις
        //}
        //args[i]=NULL;//το τελευταίιο στοιχείο πρέπει να είναι null για την execpv()
        int i = 0;
        char *p = input;

        while (*p != '\0' && i < MAX_ARGS - 1) 
        {
            // Προσπερνάμε τα αρχικά κενά (Spaces, Tabs, Newlines)
            while (*p == ' ' || *p == '\t' || *p == '\n') 
                p++;

            if (*p == '\0') break; // Τέλος του string

            // Ελέγχουμε αν ξεκινάει με αυτάκια (' ή ")
            if (*p == '"' || *p == '\'') 
            {
                char quote = *p; // Κρατάμε τι είδους αυτάκια είναι
                p++;             // Προσπερνάμε το πρώτο αυτάκι
                args[i] = p;     // Η λέξη ξεκινάει αμέσως μετά
                
                // Προχωράμε μέχρι να βρούμε το ίδιο αυτάκι που κλείνει
                while (*p != '\0' && *p != quote) 
                    p++;
                
                if (*p == quote) 
                {
                    *p = '\0'; // Σβήνουμε το αυτάκι κλεισίματος και βάζουμε τέλος string
                    p++;       // Προχωράμε παρακάτω για την επόμενη λέξη
                }
            } 
            else 
            {
                //Κανονική λέξη (χωρίς αυτάκια)
                args[i] = p;
                
                // Προχωράμε μέχρι να βρούμε κενό
                while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n') 
                    p++;
                
                if (*p != '\0') 
                {
                    *p = '\0'; // Βάζουμε τέλος string στο κενό που βρήκαμε
                    p++;       // Προχωράμε παρακάτω
                }
            }
            i++; // Πάμε στο επόμενο όρισμα
        }
        args[i] = NULL; // Τερματισμός πίνακα για την execvp

        // *** ΠΡΟΣΘΗΚΗ ΓΙΑ ΑΠΟΦΥΓΗ SEGMENTATION FAULT ***
        if (args[0] == NULL) {
            continue; // Επιστροφή στο prompt αν δεν υπάρχει εντολή
        }

        //εντολές για το cd ορίζονται χωριστά ..αν οριζόταν απο το παιδί μόλις τερμάτιζε η διεργασία του θα χανόταν η αλλαγή καταλόγου και θα παραμέναμε στον αρχικό
        if(strcmp(args[0],"cd")==0)
        {
            if (args[1]==NULL)
            {
                if (chdir(getenv("HOME")) != 0) {
                    perror("cd failed: HOME directory not set");
                }
            }
            else if(chdir(args[1]) != 0)//η chdir γυρνάει 1 σε περίπτωση αποτυχίας
            {
                perror("cd failed");
            }
             continue;
        }

        pid_t pid=fork();//Δημιουργία του "παιδιού"
        //το παιδί και ο γονέας εκτελούνται ταυτόχρονα

        if(pid==0)//είμαστε στο παιδί
        {
            execvp(args[0],args);
            
            if (errno == EACCES) {
                // Αν λέει Permission denied αλλά το αρχείο δεν υπάρχει, άλλαξε το λάθος
                if (access(args[0], F_OK) != 0) {
                    errno = ENOENT; 
                }
            }

            perror("execvp failed");
            exit(EXIT_FAILURE);
        }
        else if(pid>0)//είμαστε στον γονέα
        {
            int status;
            waitpid(pid,&status,0);//περιμένει μέχρι τον τερματισμό της διεργασίας του παιδιο΄΄υ
            // Η τιμή εξόδου (status) είναι ο κωδικός εξόδου της εντολής.
            if (WIFEXITED(status)) {
                 printf("Child exited with status %d\n", WEXITSTATUS(status));//prints exit code
            } else {
                 printf("Child process did not exit normally.\n");
            }
        }
        else{perror("fork failed");}
    }



    return 0;
    
}
    