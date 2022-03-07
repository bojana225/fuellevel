# AutoelektronikaProjekat

## Uvod

Ovaj projekat ima za zadatak da simulira merenje nivoa goriva u automobilu. Kao okruženje koristi se VisualStudio2019. 
Zadatak ovog projekta osim same funkcionalnosti je bila i implementacija Misra standarda prilikom pisanja koda.

## Ideja i zadaci: 
  1. Podaci o trenutnom stanju nivoa goriva u automobilu se dobijaju svaki sekund sa kanala 0 serijske komunikacije izraženi kao vrednost otpornosti u opsegu od 0 do 10K.
  2. Uzimati poslednjih 5 pristiglih vrednosti i računati prosek(Average).
  3. Na osnovu unetih vrednosti u_setMIN(nešto više od 0) i u_setMAX(nešto manje od 10K) izvršiti kalibraciju. Ove vrednosti šalju se preko kanala 1 serijske komunikacije. 
     Na osnovu trenutne vrednosti otpornosti u_Res koja je pristigla i ovih parametara, izračunava se trenutni nivo goriva u procentima. 
                                              FORMULA: 100 * (u_Res - u_setMIN) / (u_setMAX - u_setMIN)
  4. Ukoliko stigne poruka sa kanala 1 oblika PP, vrednost predstavlja prosečnu potrošnju tj kolika je potrošnja goriva u litrima na 100km (npr. PP8 bi označavalo 8l na 100km vožnje).
     Na osnovu ove informacije moguće je proračunati autonomiju vozila, odnosno koliko još km se automobil može kretati sa trenutnom količinom goriva u rezervoaru. Za ove potrebe
     uveden je #define MAXFUEL 40 koji označava da pun rezervoar automobila iznosi 40l, kako bi se preostala kilometraža izračunala. 
                                              FORMULA: PercentValue * MAXFUEL / u_PP
  5. Komande START i STOP imaju za zadatak da izmere kolika količina goriva u procentima je potrošena u vremenskom razmaku između slanja te dve naredbe. 
     Prilikom slanja START i STOP naredbi, u tim trenucima zapamtile su se vrednosti nivoa goriva u procentima, tako da se podatak o potrošenoj količini goriva u procentima dobija kao njihova razlika.
					      FORMULA: STARTpercent - STOPpercent
  6. Prilikom računanja nivoa goriva u procentima, ukoliko ta vrednost iznosi manje od 10% pali se prva dioda od dole u prvom stupcu LED bar-a. U suprotnom ona ne svetli.
  7. Potrebno je preko kanala 1 slati nivo goriva u procentima svakih 1s ka PC-ju.
  8. U zavisnosti koji taster je pritisnut na 7seg displeju treba prikazati trenutni nivo goriva u proceentima i vrednosti očitane otpornosti, ili autonomiju vozila i potrošenu količinu goriva u procentima. Brzina osvežavanja displeja je 100ms.
  

## Periferije

Periferije koje je potrebno koristiti su LED_bar, 7seg displej i AdvUniCom softver za simulaciju serijske komunikacije.
Prilikom pokretanja LED_bars_plus.exe navesti RRrr kao argument da bi se dobio led bar sa 2 izlazna i 2 ulazna stupca crvene boje.
Prilikom pokretanja Seg7_Mux.exe navesti kao argument broj 9, kako bi se dobio 7-seg displej sa 9 cifara.
Što se tiče serijske komunikacije, potrebno je otvoriti i kanal 0 i kanal 1. Kanal 0 se automatski otvara pokretanjem AdvUniCom.exe, 
a kanal 1 otvoriti dodavanjem broja jedan kao argument: AdvUniCom.exe 1.
Svaku od korišćenih simuliranih periferija je bilo potrebno inicijalizovati na početku programa. U tu svrhu napisana je funkcija za inicijalizaciju
u kojoj su pozvane sve potrebne init funkcije, a ona je pozvana na početku main_demo programa (samo jednom se izvršava).


## Semafori

Semafori u okviru FreeRTOS operativnog sistema imaju više mogućnosti za primenu. Najčešće se koriste za sinhronizaciju među taskovima i zaključavanje deljenih resursa (npr. globalnih promenljivih). 
U prvu svrhu se najčešće koriste klasični binarni semafori, a za zaključavanje nekog dela koda-Mutex(mutual exclusion) semafori. Glavna razlika između ova dva tipa semafora je u tome što binarni semafor jedan task “daje” 
a ostali ga “uzimaju” (vrsta signalizacije da je neki proces završen i da se može nastaviti sa radom u programu), dok je mutex mehanizam za zaključavanje, tako da mora da se “uzme” i “vrati” unutar istog taska. 
Treba biti oprezan (pogotovo sa upotrebom mutexa) jer vrlo lako može doći do dead lock-ova. Mutexi se uglavnom koriste za zaključavanje globalnih promenljivih čija se vrednost menja iz više taskova ili npr. ako se varijabla 
menja iz samo jednog taska ali se koristi 64-bitna varijabla na 32-bitnoj arhitekturi (u tom slučaju su potrebne dve operacije čitanja da bi se pročitala cela). U slučaju da se varijabla menja samo iz jednog taska a iz ostalih čita-
dovoljno je samo definisati je kao volatile, da bismo bili sigurni da je procesor neće optimizovati i da nećemo čitati zastarelu vrednost.
U ovom projektu nije bilo potrebe za korišćenjem mutexa. Korišćena su po dva semafora za svaki kanal serijske komunikacije(za signalizaciju da se TX bafer ispraznio i za signalizaciju prijema karaktera u RX bafer), 
semafor koji služi da signalizira prekid tj promenu stanja ulaznog LED stubca i još po jedan za kontrolu ispisa na serijsku komunikaciju i na 7 segmentni displej.

## Tajmeri i funkcije prekida

Svrha softverskih tejmera u FreeRTOS operativnom sistemu je skoro ista kao i hardverskih tajmera npr u kontroleru-da generišu prekide na zadat vremenski period. Razlika je u tome što ih možemo imati koliko želimo 
(dok smo sa hardverskim ograničeni), naravno imaju nešto manju preciznost u odnosu na hardverske i lakše se koriste-potrebno je pozivati samo nekoliko API funkcija sa željenim parametrima. 
U ovom projektu su korišćeni tajmeri za periodičan ispis na 7-segmentnom displeju, periodično očitavanje vrednosti otpornosti(ovo je druga opcija ako otpornosti zelimo da citamo svakih 100ms sa serijske a ne da koristimo interrupt za prijem karaktera)
i periodično slanje karaktera ka PC-ju.
Callback funkcije tajmera su korišćene kako bi se iz njih “dao” odgovarajući semafor kao signal da je potrebno osvežiti displej, poslati podatke na serijsku ili očitati vrednost otpornosti (semafori se uzimaju upravo iz taskova namenjenih za ove radnje).
U okviru programa, osim callback funkcija tajmera, korišćeno je i nekoliko funkcija prekida. Prva od njih je funkcija prekida koja se poziva prilikom promene nekog od ulaza(ulazni LED stubac) i daje semafor kao signal tasku za očitavanje stanja LED stubca da se prekid desio.
Druga dva značajna prekida se generišu kada stigne karakter preko serijske komunikacije i pri slanju kada se bafer za transmisiju isprazni. 

## Redovi

Redovi u FreeRTOS-u služe najviše za razmenu podataka između taskova kao i između taskova i prekida. Njima se može izbeći korišćenje globalnih promenljivih i samim tim zaključavanje resursa. Najčešće se koriste kao FIFO (First In First Out) baferi, tako što će prvi podatak koji
je poslat biti prvi i primljen od strane taska kojem se šalje. Podaci se kopiraju u bafer a zatim šalju. Kernel alocira automatski memoriju za skladištenje podataka u red u trenutku kada ga kreiramo. U ovom projektu su kreirani i korišćeni sledeći redovi: q_PercentValue, q_AverageValue,
q_LEDStates, q_Autonomy i q_DIFFERENCE.

## Funkcionalnosti taskova glavnog programa

Program je podeljen u 7 taskova kojima je dodeljena minimalna potrebna veličina stek memorije, odgovarajuća funkcija, ime  i prioritet (TASK_PRIO_1 je najviši, a TASK_PRIO_3 najniži prioritet). 

## Kratak pregled taskova

Glavni .c fajl ovog projekta je main_application.c

### v_FuelLevelInPercent(void* pvParameters)
Ovo je jedan od taskova u kojima se primaju vrednosti pristigle sa serijske komunikacije. Kanal 0 serijske komunikacije u ovom slučaju daje trenutnu vrednost otpornosti u automobilu. S obzirom da je ovo jedan od glavnih taskova programa, dodeljen mu je prioritet 2.
Na početku funkcije se definišu sledeće lokalne promenljive:
•u_Resistance-primljena vrednost otpornosti
•u_FuelLevelPercent-proračunata vrednost nivoa goriva u procentima
•u_Character-karakter primljen sa serijske 
•u_NumOfCharacter-broj karaktera u nizu koji je popunjen karakterima primljenim sa serijske
•a_ReceivedString[6]-niz karaktera primljenih sa serijske 
Na početku funkcije čeka se RXC_BS_0 semafor koji se daje iz prekida serijske komunikacije i označava da je pristigao karakter na kanalu 0. Zatim se očitava karakter koji je pristigao i upisujemo redom karaktere u niz sve dok dok ne dodjemo do karaktera za kraj poruke CR(0x0d).
Zatim konvertujemo pomoću funkcije atoi pristigao string u celobrojnu vrednost. Proveravamo da li očitana vrednost pripada odgovarajućem opsegu otpornosti(0 - 10000) i ako pripada računamo nivo goriva u procentima pomoću funkcije v_FuelLevelPercent. Zatim tu izračunatu vrednost upisujemo u red. 

### v_LEDReadingStates(void* p_Parameters)
Ovo je takođe jedan od prioritetnijih taskova koji služi za očitavanje ulaznog stanja LED stubca, koji u našem primeru služi za odabir šta želimo da se prikaže na displeju. Prvi stubac sa leve strane smo konfigurisali kao ulazni i njegovo stanje očitavamo. 
Na početku funkcije je definisana lokalna promenljiva u kojoj čuvamo vrednost očitanu sa ulaznog stubca LEDovki. Postoji tačno 8 dioda u svakom stubcu, tako da je jednobajtna promenljiva dovoljna za skladištenje stanja. 
Na početku while petlje čekamo semafor koji signalizira promenu na ulazu i dobija se iz prekidne rutine. Kada dobijemo semafor, očitamo stanje LED bara i pošaljemo tasku za obradu podataka.

### v_LEDStatesProcessing(void* p_Parameters)
U ovom tasku primamo stanje ulaznih LEDovki i u zavisnosti od toga šta je pritisnuto šta želimo da se prikaže na displeju. Task ima prioritet 3.
Na početku su definisane lokalne promenljive:
•u_LEDBar-promenljiva u kojoj se čuva primljeno stanje LED bara 
•u_d-u zavisnosti od primljenog stanja čuva podatak o tome šta treba da se prikaže na 7SEG displeju
•u_PercentValueReceived-primljena vrednost nivoa goriva u procentima
•STARTpercent-promenljiva u koju treba uneti neku vrednost nivoa goriva u procentima
•STOPpercent-promenljiva u koju treba uneti neku vrednost nivoa goriva u procentima
•u_DIFFERENCE-promenljiva u kojoj se računa razlika između promenljivih STARTpercent i STOPpercent
U ovom tasku se primaju dva reda-prvi dostavlja podatak o stanju ulaznih LEDovki, a drugi sadrži 
izračunatu vrednost nivoa goriva u procentima. Ako je uključena prva odole dioda, znači da je Start uključen i kao indikacija se pali prva odole dioda narednog (drugog) stubca. 
Ako je pritisnut drugi taster nultog stubca želimo da se prikaze trenutni nivo goriva i vrednost očitane otpornosti,
ako je pritisnut treći taster želimo da se prikaze autonomija vozila i potrošena količina goriva u procentima.
Zatim proveravamo članove niza u_SetStartOrStop[2]-ako je setovan prvi član niza, promenljiva STARTpercent dobija vrednost iz reda u_PercentValueReceived i u skladu sa tekstom zadatka pali se dioda trećeg stubca da signalizira aktivno merenje, a ako je setovan drugi član tada promenljiva STOPpercent dobija vrednost iz reda u_PercentValueReceived i računa se razlika ove dve vrednosti i smešta u red, a dioda se gasi. 


### v_MeasuringAverageFuelLevel(void* p_Parameters)
U ovom tasku računamo trenutnu minimalnu i maksimalnu vrednost napona iz poslednjih 5 očitavanja. Prolazimo kroz elemente niza a_Values[5] tj. kroz cirkularni bafer koji čuva poslednjih 5 očitanih vrednosti otpornosti. 
I minimalnoj i maksimalnoj vrednosti na početku dodelimo vrednost prvog člana niza, a zatim za svaki sledeći član poveravamo da li je veći od maksimuma ili manji od minimuma i ukoliko jeste, on postaje maksimum ili minimum.
Pored kalibracije, u ovom tasku se računa autonomija vozila preko gore spomenute formule i smešta se u red.

### v_7SEGWriting(void* p_Parameters)
Ovo je jedan od “izlaznih” taskova koji ispisuje podatke na periferiju- 7 segmentni displej. Ima prioritet 2. 
Korišćene lokalne promenljive u ovom tasku:
•d -u zavisnosti šta treba da se ispiše ima vrednost 2 ili 1, inicijaliyovana je na 0
•DIFF, AUT, Per, Ave-kopiraju sebi vrednost koji treba ispisati
•u_CalculatedDIFFERENCEValue, u_CalculatedAutonomyValue, u_CalculatedPercentValue, u_CalculatedAverageValue-skladište se vrednosti dobijene preko reda
Svakih 100ms iz callback funkcije tajmera dobija se semafor za ispis na displej. Primaju se po dva reda u zavisnosti šta je potrebno ispisati na displej. 
Desetice vrednosti se dobijaju deljenjem sa 10 a jedinice kao ostatak pri deljenju sa 10. Proveravamo vrednost promenljive d i na osnovu toga ispisujemo šta je potrebno. 
Ako je pritisnut drugi taster nultog stubca želimo da se prikaze trenutni nivo goriva u procentima-PercentValue i vrednost očitane otpornosti-AverageValue,
ako je pritisnut treći taster želimo da se prikaže autonomija vozila-AutonomyValue i potrošena količina goriva u procentima odnosno razlika između komandi START i STOP-DIFFERENCEValue.


### v_ReceivingCommands(void* p_Parameters)
Task je najvišeg prioriteta, 1. Služi za prijem tekstualnih komandi sa serijske komunikacije (sa kanala 1) i prosleđivanje informacija o komandi tasku za obradu i slanje podataka ka PC-ju.
Pošto je funkcija dosta dugačka i kompleksna, opisana je u komentarima u samom kodu kako bi bilo preglednije.

### v_SendingToPC(void* p_Parameters)
Ovaj task ima funkciju da ispisuje trenutnu količinu goriva prema PC-ju u procentima na kanalu 1 serijske komunikacije.  Definisanse su sledeće lokalne promenljive:
•u_CommandSize-dužina komande koju je potrebno ispisati
•u_PercentValueForWriting-promenljiva koja čuva prosečnu količinu goriva u procentima koju je potrebno ispisati, primljena je vrednost preko reda
•a_Command[13]-komanda koju je potrebno ispisati
Na početku primamo red koji sadrži podatak o trenutnoj količini goriva u procentima koju je potrebno ispisati. Čekamo da dobijemo semafor za ispis na serijsku, dobijamo ga iz callback funkcije tajmera na svakih 1000ms.
Kada pošaljemo trenutnu količinu goriva u procentima šalje se karakter za kraj poruke.
 
