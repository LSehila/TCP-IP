#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <sys/time.h>
#include <math.h>

#define TAILLE_TAB (1 << 20)  // Taille du tableau partagé
#define PORT_SERVEUR 8080  // Port du serveur
#define NB_MAX_CLIENTS 10  // Nombre maximum de clients simultanés
#define NB_CLIENTS 2 // Nombre de clients

long *tableau_general;  // Tableau des comptes partagés entre les clients
sem_t *mutex_serveur;   // Sémaphore pour synchroniser l'accès aux comptes
int clients_termines = 0; // Compteur de clients connectés

/*
 * Calcule l'équilibrage du tableau partagé.
 * Retourne un coefficient de variation.
 */
double calculer_equilibrage(long *tableau, int taille) {
    long somme = 0;
    long somme_carres = 0;

    // Calcul de la somme et de la somme des carrés
    for (int i = 0; i < taille; i++) {
        somme += tableau[i];
        somme_carres += tableau[i] * tableau[i];
    }

    // Calcul de la moyenne
    double moyenne = (double)somme / taille;

    // Calcul de la variance et de l'écart-type
    double variance = ((double)somme_carres / taille) - (moyenne * moyenne);
    double ecart_type = sqrt(variance);

    // Calcul du coefficient de variation
    double coefficient_variation = ecart_type / moyenne;
    return coefficient_variation;
}

/*
Cette fonction génère un graphe à partir des données collectées et quitte le programme.
Elle exporte les données dans un fichier texte, utilise gnuplot pour générer un graphe,
calcule la moyenne des fréquences et libère les ressources partagées.
 */
// Fonction pour générer un graphe et quitter
void genererGraphe(void) {
    printf("Génération du graphe avec gnuplot...\n");

    // Vérification des données avant écriture
    printf("Données à écrire dans data.txt :\n");
    for (int i = 0; i < 10; i++) {
        printf("Index %d: %ld\n", i, tableau_general[i]);
    }
    for (int i = TAILLE_TAB - 10; i < TAILLE_TAB; i++) {
        printf("Index %d: %ld\n", i, tableau_general[i]);
    }

    FILE *fichier_donnees = fopen("data.txt", "w");
    if (!fichier_donnees) {
        perror("Échec de la création du fichier de données");
        return;
    }

    // Écriture des données dans le fichier pour gnuplot
    for (int i = 0; i < TAILLE_TAB; i++) {
        fprintf(fichier_donnees, "%d %ld\n", i, tableau_general[i]);
    }
    fclose(fichier_donnees);

    // Exécution de gnuplot pour générer un graphe PNG
    FILE *gnuplot = popen("gnuplot", "w");
    if (!gnuplot) {
        perror("Échec du lancement de gnuplot");
        return;
    }

    fprintf(gnuplot, "set terminal png size 1920,1080\n");
    fprintf(gnuplot, "set output 'graph.png'\n");
    fprintf(gnuplot, "set title 'Distribution des nombres aléatoires'\n");
    fprintf(gnuplot, "set xlabel 'Index'\n");
    fprintf(gnuplot, "set ylabel 'Fréquence'\n");
    fprintf(gnuplot, "plot 'data.txt' with lines title 'Fréquence'\n");

    fflush(gnuplot);
    int ret = pclose(gnuplot);

    if (ret != 0) {
        printf("Erreur de gnuplot : %d\n", ret);
        return;
    }

    // Vérification si le graphe a été créé correctement
    if (access("graph.png", F_OK) == 0) {
        printf("Graphe créé avec succès sous le nom 'graph.png'\n");
        printf("Merci pour votre patience. Le programme va maintenant s'arrêter.\n");
    } else {
        perror("Échec de la génération du graphe");
    }

    // Calcul de la moyenne des fréquences
    long somme_frequences = 0;
    for (int i = 0; i < TAILLE_TAB; i++) {
        somme_frequences += tableau_general[i];
    }
    double moyenne_frequences = (double)somme_frequences / TAILLE_TAB;
    printf("Moyenne des fréquences : %.2f\n", moyenne_frequences);

    // Fermeture des ressources partagées
    sem_close(mutex_serveur);
    sem_unlink("mutex_serveur");
    munmap(tableau_general, TAILLE_TAB * sizeof(long));
    shm_unlink("comptes_serveur");
    exit(0);
}

/*
Cette fonction Gère la communication avec un client connecté. Cette fonction reçoit un tableau de fréquences du client,
met à jour les comptes partagés et ferme la connexion.
 */
void *gerer_client(void *arg) {
    int sock_fd = *(int *)arg;
    free(arg);

    // Allocation de mémoire pour le tableau local du client
    long *comptes_client = calloc(TAILLE_TAB, sizeof(long));
    if (comptes_client == NULL) {
        perror("Échec de calloc");
        close(sock_fd);
        pthread_exit(NULL);
    }

    // Réception des données du client
    size_t total_recu = 0;
    while (total_recu < TAILLE_TAB * sizeof(long)) {
        ssize_t octets_lus = read(sock_fd, ((char *)comptes_client) + total_recu,
                                  TAILLE_TAB * sizeof(long) - total_recu);
        if (octets_lus < 0) {
            perror("Échec de read");
            free(comptes_client);
            close(sock_fd);
            pthread_exit(NULL);
        } else if (octets_lus == 0) {
            printf("Client déconnecté prématurément (socket fd: %d)\n", sock_fd);
            free(comptes_client);
            close(sock_fd);
            pthread_exit(NULL);
        }
        total_recu += octets_lus;
    }

    // Log des données reçues
    printf("Données reçues du client :\n");
    for (int i = 0; i < 10; i++) {
        printf("Index %d: %ld\n", i, comptes_client[i]);
    }
    for (int i = TAILLE_TAB - 10; i < TAILLE_TAB; i++) {
        printf("Index %d: %ld\n", i, comptes_client[i]);
    }

    // Mise à jour des comptes partagés
    sem_wait(mutex_serveur);
    printf("Avant mise à jour du tableau comptes_serveur :\n");
    for (int i = 0; i < 10; i++) {
        printf("Index %d: %ld\n", i, tableau_general[i]);
    }

    for (int i = 0; i < TAILLE_TAB; i++) {
        tableau_general[i] += comptes_client[i];
    }

    printf("Après mise à jour du tableau comptes_serveur :\n");
    for (int i = 0; i < 10; i++) {
        printf("Index %d: %ld\n", i, tableau_general[i]);
    }
    sem_post(mutex_serveur);

    free(comptes_client);
    close(sock_fd);

    // Incrémenter le compteur de clients terminés
    clients_termines++;

    // Si tous les clients ont terminé, générer le graphe
    if (clients_termines >= NB_CLIENTS) {
        genererGraphe();
    }

    pthread_exit(NULL);
}

/*
Notre fonction main Initialise la mémoire partagée, le sémaphore, et le socket. Écoute les connexions des clients et les traite en parallèle via des threads.
*/
int main() {
    // Mesure du temps d'exécution
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    // Configuration de la mémoire partagée
    int shm_fd = shm_open("comptes_serveur", O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("Échec de shm_open");
        exit(EXIT_FAILURE);
    }
    if (ftruncate(shm_fd, TAILLE_TAB * sizeof(long)) < 0) {
        perror("Échec de ftruncate");
        close(shm_fd);
        exit(EXIT_FAILURE);
    }
    tableau_general = mmap(NULL, TAILLE_TAB * sizeof(long), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (tableau_general == MAP_FAILED) {
        perror("Échec de mmap");
        close(shm_fd);
        exit(EXIT_FAILURE);
    }
    memset(tableau_general, 0, TAILLE_TAB * sizeof(long));  // Initialisation à zéro

    // Log de l'initialisation
    printf("Initialisation du tableau comptes_serveur :\n");
    for (int i = 0; i < 10; i++) {
        printf("Index %d: %ld\n", i, tableau_general[i]);
    }

    // Création du sémaphore
    mutex_serveur = sem_open("mutex_serveur", O_CREAT, 0644, 1);
    if (mutex_serveur == SEM_FAILED) {
        perror("Échec de sem_open");
        munmap(tableau_general, TAILLE_TAB * sizeof(long));
        close(shm_fd);
        exit(EXIT_FAILURE);
    }

    // Création et configuration du socket
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("Échec de la création du socket");
        sem_close(mutex_serveur);
        munmap(tableau_general, TAILLE_TAB * sizeof(long));
        close(shm_fd);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in adresse_serveur = {0};
    adresse_serveur.sin_family = AF_INET;
    adresse_serveur.sin_port = htons(PORT_SERVEUR);
    adresse_serveur.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock_fd, (struct sockaddr *)&adresse_serveur, sizeof(adresse_serveur)) < 0) {
        perror("Échec de bind");
        close(sock_fd);
        sem_close(mutex_serveur);
        munmap(tableau_general, TAILLE_TAB * sizeof(long));
        close(shm_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(sock_fd, NB_MAX_CLIENTS) < 0) {
        perror("Échec de listen");
        close(sock_fd);
        sem_close(mutex_serveur);
        munmap(tableau_general, TAILLE_TAB * sizeof(long));
        close(shm_fd);
        exit(EXIT_FAILURE);
    }
    printf("Serveur en écoute sur le port %d\n", PORT_SERVEUR);

    while (1) {
        int *client_sock = malloc(sizeof(int));
        if (client_sock == NULL) {
            perror("Échec de malloc");
            continue;
        }

        *client_sock = accept(sock_fd, NULL, NULL);
        if (*client_sock < 0) {
            perror("Échec de accept");
            free(client_sock);
            continue;
        }
        printf("Client connecté (socket fd: %d)\n", *client_sock);

        pthread_t thread;
        if (pthread_create(&thread, NULL, gerer_client, client_sock) != 0) {
            perror("Échec de pthread_create");
            close(*client_sock);
            free(client_sock);
        } else {
            pthread_detach(thread);
        }
    }

    return 0;
}