#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//ECRAN OLED

const int LARGEUR_ECRAN = 128;
const int HAUTEUR_ECRAN = 32;
const int RESET_OLED = -1;

const int BROCHE_SDA = 8;
const int BROCHE_SCL = 9;

//MODULE UWB

const int BROCHE_RX_UWB = 20;
const int BROCHE_TX_UWB = 21;
const char ADRESSE_TAG[] = "TAG12345";


//REGLAGE

const unsigned long INTERVALLE_DEMANDE_MS = 600;//Mesure automatique toutes les 600ms
const unsigned long TIMEOUT_REPONSE_MS = 2000;
const unsigned long INTERVALLE_AFFICHAGE_MS = 500;

const int DISTANCE_MAX_CM = 20000;//
const int SAUT_MAX_CM = 300;

const int TAILLE_FILTRE = 7;
const float COEFFICIENT_LISSAGE = 0.35;
//OBJETS

Adafruit_SSD1306 ecran(LARGEUR_ECRAN, HAUTEUR_ECRAN, &Wire, RESET_OLED);
HardwareSerial uwb(1);

// VARIABLE

String ligneRecue = "";

unsigned long dernierEnvoi = 0;
unsigned long derniereReponse = 0;
unsigned long dernierAffichage = 0;

bool attenteReponse = false;
bool liaisonOK = false;

int mesures[TAILLE_FILTRE];
int nombreMesures = 0;
int indexMesure = 0;

float distanceLissee = -1;
int distanceAffichee = -1;

///////////////////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(BROCHE_SDA, BROCHE_SCL);

  if (!ecran.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) {
      delay(100);
    }
  }

  ecran.setTextColor(SSD1306_WHITE);
  afficherMessage("UWB TRACKER", "Demarrage...");

  uwb.begin(115200, SERIAL_8N1, BROCHE_RX_UWB, BROCHE_TX_UWB);//initalisation de l'UWB
  delay(1000);

  viderBufferUWB();

  afficherMessage("UWB TRACKER", "Pret");
}

///////////////////////////////////////////////////////////////

void loop() {
  lireUWB();

  gererTimeout();

  if (!attenteReponse && millis() - dernierEnvoi >= INTERVALLE_DEMANDE_MS) {
    envoyerDemandeDistance();
  }

  if (millis() - dernierAffichage >= INTERVALLE_AFFICHAGE_MS) {
    dernierAffichage = millis();

    if (distanceAffichee > 0) {
      afficherDistance(distanceAffichee);
    } else if (!liaisonOK && millis() > 3000) {
      afficherMessage("UWB TRACKER", "Aucune mesure");
    }
  }
}

// fonction de l'ENVOI de COMMANDE

void envoyerDemandeDistance() {
  char commande[64];

  sprintf(commande, "AT+ANCHOR_SEND=%s,4,TEST\r\n", ADRESSE_TAG);//interrogation du module TAG

  Serial.print("ENVOI : ");
  Serial.print(commande);

  uwb.print(commande);

  dernierEnvoi = millis();
  attenteReponse = true;
}

// LECTURE de l'uwb

void lireUWB() {
  while (uwb.available()) {
    char caractere = uwb.read();

    if (caractere == '\r' || caractere == '\n') {
      if (ligneRecue.length() > 0) {
        analyserLigne(ligneRecue);
        ligneRecue = "";
      }
    } else {
      if (ligneRecue.length() < 120) {
        ligneRecue += caractere;
      } else {
        ligneRecue = "";
      }
    }
  }
}



void analyserLigne(String ligne) {
  Serial.print("RECU : ");
  Serial.println(ligne);

  if (ligne.startsWith("+OK")) {
    return;
  }

  if (ligne.startsWith("+ERR")) {
    attenteReponse = false;
    liaisonOK = false;
    return;
  }

  int positionCm = ligne.indexOf("cm");
  if (positionCm < 0) {
    return;
  }

  int positionVirgule = ligne.lastIndexOf(',', positionCm);
  if (positionVirgule < 0) {
    return;
  }

  String texteDistance = ligne.substring(positionVirgule + 1, positionCm);
  texteDistance.trim();

  int distanceCm = texteDistance.toInt();

  if (distanceCm <= 0 || distanceCm > DISTANCE_MAX_CM) {
    attenteReponse = false;
    return;
  }

  int distanceFiltree = filtrerDistance(distanceCm);//filtage des mesures

  if (distanceFiltree > 0) {
    distanceAffichee = distanceFiltree;
    liaisonOK = true;
    derniereReponse = millis();
  }

  attenteReponse = false;
}
// TIMEOUT

void gererTimeout() {//limitation des blocages
  if (attenteReponse && millis() - dernierEnvoi > TIMEOUT_REPONSE_MS) {
    Serial.println("TIMEOUT UWB");

    attenteReponse = false;
    liaisonOK = false;
    ligneRecue = "";

    viderBufferUWB();
  }
}

void viderBufferUWB() {
  while (uwb.available()) {
    uwb.read();
  }
}

//FILTRAGE

int filtrerDistance(int nouvelleDistanceCm) {
  if (distanceLissee > 0 && abs(nouvelleDistanceCm - distanceLissee) > SAUT_MAX_CM) {
    return (int)(distanceLissee + 0.5);
  }

  mesures[indexMesure] = nouvelleDistanceCm;
  indexMesure = (indexMesure + 1) % TAILLE_FILTRE;

  if (nombreMesures < TAILLE_FILTRE) {
    nombreMesures++;
  }

  int mediane = calculerMediane();

  if (distanceLissee < 0) {
    distanceLissee = mediane;
  } else {
    distanceLissee = distanceLissee + COEFFICIENT_LISSAGE * (mediane - distanceLissee);
  }

  return (int)(distanceLissee + 0.5);
}

int calculerMediane() {
  int copie[TAILLE_FILTRE];

  for (int i = 0; i < nombreMesures; i++) {
    copie[i] = mesures[i];
  }

  for (int i = 0; i < nombreMesures - 1; i++) {
    for (int j = i + 1; j < nombreMesures; j++) {
      if (copie[j] < copie[i]) {
        int temp = copie[i];
        copie[i] = copie[j];
        copie[j] = temp;
      }
    }
  }

  return copie[nombreMesures / 2];
}


// AFFICHAGE
void afficherDistance(int distanceCm) {//
  ecran.clearDisplay();

  ecran.setTextSize(1);
  ecran.setCursor(0, 0);
  ecran.println("DISTANCE");

  ecran.setTextSize(3);
  ecran.setCursor(0, 12);

  if (distanceCm < 100) {
    ecran.print(distanceCm);
    ecran.setTextSize(1);
    ecran.print(" cm");
  } else {
    float distanceMetres = distanceCm / 100.0;
    ecran.print(distanceMetres, 2);
    ecran.setTextSize(1);
    ecran.print(" m");
  }

  ecran.display();
}

void afficherMessage(const char* titre, const char* message) {
  ecran.clearDisplay();

  ecran.setTextSize(1);
  ecran.setCursor(0, 0);
  ecran.println(titre);

  ecran.setCursor(0, 16);
  ecran.println(message);

  ecran.display();
}
