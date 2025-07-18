# Usa Ubuntu come immagine di base
FROM ubuntu:22.04

# Evita domande durante l’installazione dei pacchetti
ENV DEBIAN_FRONTEND=noninteractive

# Installa dipendenze di sistema
RUN apt-get update && apt-get install -y \
    wget curl git bzip2 sudo build-essential \
    && rm -rf /var/lib/apt/lists/*

# Crea una cartella utente
WORKDIR /opt/app

# Installa Anaconda
ENV CONDA_DIR=/opt/conda
ENV PATH=$CONDA_DIR/bin:$PATH

RUN wget https://repo.anaconda.com/archive/Anaconda3-2023.07-2-Linux-x86_64.sh -O /tmp/anaconda.sh && \
    bash /tmp/anaconda.sh -b -p $CONDA_DIR && \
    rm /tmp/anaconda.sh

# Copia i file del tuo progetto
COPY . /opt/app

# Rendi tutti i file leggibili ed eseguibili da tutti gli utenti
RUN chmod -R a+rX /opt/app && find /opt/app -type d -exec chmod a+w {} +


# Crea un ambiente conda e installa Gurobi + te-ccl
# Crea l’ambiente Conda e installa le dipendenze
RUN conda create -n myenv python=3.10 -y && \
    conda install -n myenv -c gurobi gurobi -y && \
    conda run -n myenv pip install ./te-ccl-dev/TE-CCL/


# Imposta variabile ambiente per Gurobi (ATTENZIONE: serve la licenza)
ENV GRB_LICENSE_FILE=/opt/app/gurobi.lic

# Imposta l’ambiente da attivare all'avvio
SHELL ["/bin/bash", "-c"]
ENTRYPOINT ["./entrypoint.sh"]

    