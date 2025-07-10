#include <iostream>
#include <vector>
#include <string>
#include <future>
#include <thread>
#include <cstdlib>

using namespace std;

// Nome do executavel no diretorio corrente
//SA
const string EXECUTAVEL = "C:/Users/afons/Pessoal/03.Pos-Graduacao/ITA/Doutorado/01.Disciplinas/PO-205_Metaheuristica/Projeto/Codigos/RodarInstanciasParalelo/SA/04.Simulated_Annealing_v14bis_iRace_Windows_v01.exe";
//GA
//const string EXECUTAVEL = "C:/Users/afons/Pessoal/03.Pos-Graduacao/ITA/Doutorado/01.Disciplinas/PO-205_Metaheuristica/Projeto/Codigos/RodarInstanciasParalelo/GA/05.Genetic_Algorithm_v03g+iRace_Windows_v03.exe";


// Numero maximo de threads paralelas
const int NUM_THREADS = 8;

// Parametros fixos (obtidos via iRace)
//SA
// NUM_SOLUCOES_ALEATORIAS, NUM_TOP_K_TIPOS_BIN, K_TROCA_ITENS_BIN, CRITERIO_MELHOR_ENTRE_N, LIMIAR_OCUPACAO_BIN, MAX_TENTATIVAS, TEMPO_TROCA_VIZINHANCA_MS, 
// LIMIAR_ENTROPIA_CRITICA, LIMIAR_OCUPACAO_BAIXA, LIMIAR_OCUPACAO_TROCA_IMEDIATA, LIMIAR_OCUPACAO_INTERMEDIARIA, LIMIAR_OCUPACAO_ALTA, LIMIAR_OCUPACAO_MUITO_ALTA, 
// LIMIAR_ENTROPIA_MODERADA, LIMIAR_ENTROPIA_ALTA, LIMIAR_ENTROPIA_MUITO_ALTA, LIMIAR_STD_OCUPACAO_MODERADO, LIMIAR_STD_OCUPACAO_ALTO, LIMIAR_SKEW_PESOS;
const string parametros_otimos = "32 3 7 0 0.15 100 5000 0.65 0.60 0.70 0.75 0.85 0.88 0.80 0.85 0.90 0.25 0.30 1.5";
const string seed = "552381141";


//GA
//taxa_mutacao_base, AUMENTO_TAXA_MUTACAO, LIMIAR_DIVERSIDADE, FRACAO_REINICIALIZAR, JANELA_ESTAGNACAO_MS, FATOR_POPULACAO, PROB_CRUZAMENTO, NUM_ELITES, PCT_POPULACAO_ALEATORIAS
//const string parametros_otimos = "0.2303 0.2936 0.4300 0.4531 1309 1.6738 0.9370 9 0.85";
//const string seed = "2080421301";

// Lista de instancias (definida diretamente no código)

const vector<string> nomes_instancias_1x = { "HSconv100-3-6.txt"};
/*
const vector<string> nomes_instancias_1x = {
    "prob_25_1.txt", "prob_25_2.txt", "prob_25_3.txt", "prob_25_4.txt", "prob_25_5.txt", "prob_25_6.txt", "prob_25_7.txt", "prob_25_8.txt",
    "prob_25_9.txt", "prob_25_10.txt", "prob_50_1.txt", "prob_50_2.txt", "prob_50_3.txt", "prob_50_4.txt", "prob_50_5.txt",
    "prob_50_6.txt", "prob_50_7.txt", "prob_50_8.txt", "prob_50_9.txt", "prob_50_10.txt", "prob_100_1.txt", "prob_100_2.txt",
    "prob_100_3.txt", "prob_100_4.txt", "prob_100_5.txt", "prob_100_6.txt", "prob_100_7.txt", "prob_100_8.txt", "prob_100_9.txt",
    "prob_100_10.txt", "prob_200_1.txt", "prob_200_2.txt", "prob_200_3.txt", "prob_200_4.txt", "prob_200_5.txt", "prob_200_6.txt",
    "prob_200_7.txt", "prob_200_8.txt", "prob_200_9.txt", "prob_200_10.txt", "prob_500_1.txt", "prob_500_2.txt", "prob_500_3.txt",
    "prob_500_4.txt", "prob_500_5.txt", "prob_500_6.txt", "prob_500_7.txt", "prob_500_8.txt", "prob_500_9.txt", "prob_500_10.txt",
    "HSlin100-1-1.txt", "HSlin100-1-2.txt", "HSlin100-1-3.txt", "HSlin100-1-4.txt", "HSlin100-1-5.txt", "HSlin100-1-6.txt", "HSlin100-1-7.txt",
    "HSlin100-1-8.txt", "HSlin100-1-9.txt", "HSlin100-1-10.txt", "HSlin200-1-1.txt", "HSlin200-1-2.txt", "HSlin200-1-3.txt", "HSlin200-1-4.txt",
    "HSlin200-1-5.txt", "HSlin200-1-6.txt", "HSlin200-1-7.txt", "HSlin200-1-8.txt", "HSlin200-1-9.txt", "HSlin200-1-10.txt", "HSlin500-1-1.txt",
    "HSlin500-1-2.txt", "HSlin500-1-3.txt", "HSlin500-1-4.txt", "HSlin500-1-5.txt", "HSlin500-1-6.txt", "HSlin500-1-7.txt", "HSlin500-1-8.txt",
    "HSlin500-1-9.txt", "HSlin500-1-10.txt", "HSlin1000-1-1.txt", "HSlin1000-1-2.txt", "HSlin1000-1-3.txt", "HSlin1000-1-4.txt", "HSlin1000-1-5.txt",
    "HSlin1000-1-6.txt", "HSlin1000-1-7.txt", "HSlin1000-1-8.txt", "HSlin1000-1-9.txt", "HSlin1000-1-10.txt", "HSlin2000-1-1.txt", "HSlin2000-1-2.txt",
    "HSlin2000-1-3.txt", "HSlin2000-1-4.txt", "HSlin2000-1-5.txt", "HSlin2000-1-6.txt", "HSlin2000-1-7.txt", "HSlin2000-1-8.txt", "HSlin2000-1-9.txt",
    "HSlin2000-1-10.txt", "HSconc100-2-1.txt", "HSconc100-2-2.txt", "HSconc100-2-3.txt", "HSconc100-2-4.txt", "HSconc100-2-5.txt", "HSconc100-2-6.txt",
    "HSconc100-2-7.txt", "HSconc100-2-8.txt", "HSconc100-2-9.txt", "HSconc100-2-10.txt", "HSconc200-2-1.txt", "HSconc200-2-2.txt", "HSconc200-2-3.txt",
    "HSconc200-2-4.txt", "HSconc200-2-5.txt", "HSconc200-2-6.txt", "HSconc200-2-7.txt", "HSconc200-2-8.txt", "HSconc200-2-9.txt", "HSconc200-2-10.txt",
    "HSconc500-2-1.txt", "HSconc500-2-2.txt", "HSconc500-2-3.txt", "HSconc500-2-4.txt", "HSconc500-2-5.txt", "HSconc500-2-6.txt", "HSconc500-2-7.txt",
    "HSconc500-2-8.txt", "HSconc500-2-9.txt", "HSconc500-2-10.txt", "HSconc1000-2-1.txt", "HSconc1000-2-2.txt", "HSconc1000-2-3.txt", "HSconc1000-2-4.txt",
    "HSconc1000-2-5.txt", "HSconc1000-2-6.txt", "HSconc1000-2-7.txt", "HSconc1000-2-8.txt", "HSconc1000-2-9.txt", "HSconc1000-2-10.txt", "HSconc2000-2-1.txt",
    "HSconc2000-2-2.txt", "HSconc2000-2-3.txt", "HSconc2000-2-4.txt", "HSconc2000-2-5.txt", "HSconc2000-2-6.txt", "HSconc2000-2-7.txt", "HSconc2000-2-8.txt",
    "HSconc2000-2-9.txt", "HSconc2000-2-10.txt", "HSconv100-3-1.txt", "HSconv100-3-2.txt", "HSconv100-3-3.txt", "HSconv100-3-4.txt", "HSconv100-3-5.txt",
    "HSconv100-3-6.txt", "HSconv100-3-7.txt", "HSconv100-3-8.txt", "HSconv100-3-9.txt", "HSconv100-3-10.txt", "HSconv200-3-1.txt", "HSconv200-3-2.txt",
    "HSconv200-3-3.txt", "HSconv200-3-4.txt", "HSconv200-3-5.txt", "HSconv200-3-6.txt", "HSconv200-3-7.txt", "HSconv200-3-8.txt", "HSconv200-3-9.txt",
    "HSconv200-3-10.txt", "HSconv500-3-1.txt", "HSconv500-3-2.txt", "HSconv500-3-3.txt", "HSconv500-3-4.txt", "HSconv500-3-5.txt", "HSconv500-3-6.txt",
    "HSconv500-3-7.txt", "HSconv500-3-8.txt", "HSconv500-3-9.txt", "HSconv500-3-10.txt", "HSconv1000-3-1.txt", "HSconv1000-3-2.txt", "HSconv1000-3-3.txt",
    "HSconv1000-3-4.txt", "HSconv1000-3-5.txt", "HSconv1000-3-6.txt", "HSconv1000-3-7.txt", "HSconv1000-3-8.txt", "HSconv1000-3-9.txt", "HSconv1000-3-10.txt",
    "HSconv2000-3-1.txt", "HSconv2000-3-2.txt", "HSconv2000-3-3.txt", "HSconv2000-3-4.txt", "HSconv2000-3-5.txt", "HSconv2000-3-6.txt", "HSconv2000-3-7.txt",
    "HSconv2000-3-8.txt", "HSconv2000-3-9.txt", "HSconv2000-3-10.txt" };

*/

/**
 * @brief Repete o conteudo de um vetor base por N vezes consecutivas.
 *
 * @tparam T Tipo dos elementos do vetor.
 * @param base Vetor com os elementos a serem repetidos.
 * @param repeticoes Numero de vezes que o vetor base deve ser concatenado.
 * @return Vetor com o conteudo repetido.
 */
template<typename T>
vector<T> gerarListaCombinada(const vector<T>& base, int repeticoes)
{
    vector<T> resultado;
    resultado.reserve(base.size() * repeticoes); // opcional: otimiza alocacao

    for (int r = 0; r < repeticoes; ++r) {
        resultado.insert(resultado.end(), base.begin(), base.end());
    }

    return resultado;
}


/**
 * @brief Executa o comando externo com os parametros completos e a instancia
 */
void rodarInstancia(int id_config, int id_instancia, const string& instancia) {
    string comando = EXECUTAVEL + " " +
        to_string(id_config) + " " +
        to_string(id_instancia) + " " +
        seed + " " +
        instancia + " " +
        parametros_otimos;
   
    system(comando.c_str());
    
}

/**
 * @brief Funcao principal: paraleliza execucao do executavel para todas as instancias
 */
int main() {


    // =======================================================
    // GERA LISTA FINAL DE INSTANCIAS PARA RODAR O EXPERIMENTO
    // =======================================================

    // Numero de repeticoes de cada instancia
    const int REPETICOES = 1;

    // Multiplica a lista base de nomes de instancias por REPETICOES    
    vector<string> instancias = gerarListaCombinada(nomes_instancias_1x, REPETICOES);

    vector<future<void>> tarefas;

    cout << "\n Inicio da execucao ..." << endl;

    for (size_t i = 0; i < instancias.size(); ++i) {
        int id_config = 1;
        int id_instancia = static_cast<int>(i) + 1;

        tarefas.emplace_back(async(launch::async, rodarInstancia, id_config, id_instancia, instancias[i]));

        if (tarefas.size() >= NUM_THREADS) {
            for (auto& t : tarefas) t.get();
            tarefas.clear();
        }
    }

    // Espera final das ultimas instancias
    for (auto& t : tarefas) t.get();

    cout << "\nTodas as instancias foram processadas com sucesso!" << endl;
    return 0;
}
