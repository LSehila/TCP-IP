#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <sys/random.h>
#include <string.h>

#define TAILLE_TAB (1 << 20)  // Taille des tableau locaux
#define NB_APPELS_PAR_PROCESSUS 50000000000  // Nombre d'appels par processus
#define NB_PROCESSUS 5  // Nombre de processus
#define PORT_SERVEUR 8080  // Port du serveur
#define IP_SERVEUR "127.0.0.1" //ip du serveur

long *tableau_local;  // Tableau partagé entre les processus
sem_t *mutex_partage;    // Sémaphore pour protéger le tableau partagé




// Fonction pour générer un nombre aléatoire
unsigned int generer_nb_alea() {
    uint32_t nombre_aleatoire;
    if (getrandom(&nombre_aleatoire, sizeof(nombre_aleatoire), 0) < 0) {
        perror("Échec de getrandom");
        exit(EXIT_FAILURE);
    }
    return nombre_aleatoire % TAILLE_TAB;  // Assurez-vous que cela couvre toute la plage
}




// Fonction exécutée par chaque processus
void travail_processus(int id_processus) {
    long *comptes_locaux = calloc(TAILLE_TAB, sizeof(long));
    if (comptes_locaux == NULL) {
        perror("Échec de calloc");
        exit(EXIT_FAILURE);
    }

    // Incrémenter les tableaux locaux pour un certain nombre de fois
    for (int i = 0; i < NB_APPELS_PAR_PROCESSUS; i++) {
        unsigned int numero_aleatoire = generer_nb_alea();
        comptes_locaux[numero_aleatoire]++;
    }

    // Synchronisation de l'accès au tableau partagé via un sémaphore
    sem_wait(mutex_partage);
    for (int i = 0; i < TAILLE_TAB; i++) {
        tableau_local[i] += comptes_locaux[i];
    }
    sem_post(mutex_partage);

    printf("Processus %d a terminé la mise à jour des comptes partagés\n", id_processus);

    free(comptes_locaux);
}





int main() {

    int sock_fd;
    struct sockaddr_in adresse_serveur;

    // Création de la mémoire partagée
    int shm_fd = shm_open("comptes_partages_client", O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("Échec de shm_open");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd, TAILLE_TAB * sizeof(long)) < 0) {
        perror("Échec de ftruncate");
        close(shm_fd);
        exit(EXIT_FAILURE);
    }



    /*Mapping de la mémoire partagée pour permettre  
    à plusierus processus d'accéder à la même région de mémoire */

    tableau_local = mmap(NULL, TAILLE_TAB * sizeof(long), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (tableau_local == MAP_FAILED) {
        perror("Échec de mmap");
        close(shm_fd);
        exit(EXIT_FAILURE);
    }


    // Initialisation des tableaux locaux à zéro
    for (int i = 0; i < TAILLE_TAB; i++) {
        tableau_local[i] = 0;
    }

    // Création du sémaphore pour la synchronisation
    mutex_partage = sem_open("mutex_partage_client", O_CREAT, 0644, 1);
    if (mutex_partage == SEM_FAILED) {
        perror("Échec de sem_open");
        munmap(tableau_local, TAILLE_TAB * sizeof(long));
        close(shm_fd);
        exit(EXIT_FAILURE);
    }

    pid_t pids[NB_PROCESSUS];
    for (int i = 0; i < NB_PROCESSUS; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("Échec de fork");
            exit(EXIT_FAILURE);
        } else if (pids[i] == 0) {
            travail_processus(i);  // Exécution du travail par chaque processus
            exit(EXIT_SUCCESS);
        }
    }

    // Attente de la fin des processus enfants
    for (int i = 0; i < NB_PROCESSUS; i++) {
        wait(NULL);
    }

    // Affichage des premiers et derniers éléments du tableau partagé
    printf("Les 10 premières valeurs envoyées par le client :\n");
    for (int i = 0; i < 10; i++) {
        printf("Index %d: %ld\n", i, tableau_local[i]);
    }

    printf("Les 10 dérnières valeurs envoyées par le client :\n");
    for (int i = TAILLE_TAB - 10; i < TAILLE_TAB; i++) {
        printf("Index %d: %ld\n", i, tableau_local[i]);
    }

    // Connexion au serveur
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Échec de la création du socket");
        munmap(tableau_local, TAILLE_TAB * sizeof(long));
        close(shm_fd);
        sem_close(mutex_partage);
        exit(EXIT_FAILURE);
    }

    memset(&adresse_serveur, 0, sizeof(adresse_serveur));
    adresse_serveur.sin_family = AF_INET;
    adresse_serveur.sin_port = htons(PORT_SERVEUR);
    if (inet_pton(AF_INET, IP_SERVEUR, &adresse_serveur.sin_addr) <= 0) {
        perror("Échec de inet_pton");
        close(sock_fd);
        munmap(tableau_local, TAILLE_TAB * sizeof(long));
        close(shm_fd);
        sem_close(mutex_partage);
        exit(EXIT_FAILURE);
    }

    // Affichage de message de connexion avant l'envoi des données
    printf("Connexion au serveur réussie, veuillez patienter...\n");

    if (connect(sock_fd, (struct sockaddr *)&adresse_serveur, sizeof(adresse_serveur)) < 0) {
        perror("Échec de la connexion");
        close(sock_fd);
        munmap(tableau_local, TAILLE_TAB * sizeof(long));
        close(shm_fd);
        sem_close(mutex_partage);
        exit(EXIT_FAILURE);
    }
    printf("Connecté au serveur (socket fd: %d)\n", sock_fd);

    // Envoi des données au serveur
    printf("Envoi des comptes partagés au serveur...\n");
    size_t total_envoye = 0;
    while (total_envoye < TAILLE_TAB * sizeof(long)) {
        ssize_t envoye = send(sock_fd, ((char *)tableau_local) + total_envoye,
                              TAILLE_TAB * sizeof(long) - total_envoye, 0);
        if (envoye < 0) {
            perror("Échec de send");
            close(sock_fd);
            munmap(tableau_local, TAILLE_TAB * sizeof(long));
            close(shm_fd);
            sem_close(mutex_partage);
            exit(EXIT_FAILURE);
        }
        total_envoye += envoye;
        printf("Envoyé %zd octets, total envoyé : %lu octets\n", envoye, total_envoye);
    }
    printf("Données envoyées avec succès au serveur\n");

    close(sock_fd);
    munmap(tableau_local, TAILLE_TAB * sizeof(long));
    close(shm_fd);
    sem_close(mutex_partage);
    shm_unlink("comptes_partages_client");
    sem_unlink("mutex_partage_client");

    return 0;
}