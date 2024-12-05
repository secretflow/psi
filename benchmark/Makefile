include docker-compose/.env
include .env

USER := $(shell whoami)
DOCKER_PROJ_NAME := ${DOCKER_PROJ_PREFIX}_${USER}


.PHONY: start-docker clean mock-data analysis

default: all

clean:
	@[ -d "$(PWD)/docker-compose/logs" ] && rm -rf $(PWD)/docker-compose/logs && echo "Directory removed." || echo "Directory not exists."
	@[ -d "$(PWD)/docker-compose/tmp_data" ] && rm -rf $(PWD)/docker-compose/tmp_data && echo "Directory removed." || echo "Directory not exists."
	@(docker compose -p ${DOCKER_PROJ_NAME} down)

mock-data:
	[ ! -d "$(PWD)/docker-compose/tmp_data" ] && mkdir -p "$(PWD)/docker-compose/tmp_data" && echo "Directory created." || echo "Directory already exists."
	@(python $(PWD)/../examples/psi/generate_psi_data.py --receiver_item_cnt ${RECEIVER_ITEM_CNT} \
		--sender_item_cnt ${SENDER_ITEM_CNT} --intersection_cnt ${INTERSECTION_CNT} --id_cnt ${ID_CNT} \
		--receiver_path docker-compose/tmp_data/receiver_input.csv --sender_path docker-compose/tmp_data/sender_input.csv \
		--intersection_path docker-compose/tmp_data/intersection.csv)

all: clean mock-data start-docker analysis
	@echo "well done!"

analysis:
	@[ ! -d "$(PWD)/docker-compose/logs" ] && mkdir -p "$(PWD)/docker-compose/logs" && echo "Directory created." || echo "Directory already exists."
	@[ ! -d "$(PWD)/docker-compose/logs/receiver" ] && mkdir -p "$(PWD)/docker-compose/logs/receiver" && echo "Directory created." || echo "Directory already exists."
	@[ ! -d "$(PWD)/docker-compose/logs/sender" ] && mkdir -p "$(PWD)/docker-compose/logs/sender" && echo "Directory created." || echo "Directory already exists."
	nohup python $(PWD)/stats.py $(DOCKER_PROJ_NAME)-psi-sender-1 $(PWD)/docker-compose/logs/sender/stats.csv >/dev/null 2>&1 &
	python $(PWD)/stats.py $(DOCKER_PROJ_NAME)-psi-receiver-1 $(PWD)/docker-compose/logs/receiver/stats.csv
	python $(PWD)/plot_csv_data.py $(PWD)/docker-compose/logs/receiver/stats.csv $(PWD)/docker-compose/logs/receiver
	python $(PWD)/plot_csv_data.py $(PWD)/docker-compose/logs/sender/stats.csv $(PWD)/docker-compose/logs/sender
	docker logs $(DOCKER_PROJ_NAME)-psi-sender-1 > $(PWD)/docker-compose/logs/sender/psi.log
	docker logs $(DOCKER_PROJ_NAME)-psi-receiver-1 > $(PWD)/docker-compose/logs/receiver/psi.log

start-docker:
	@(cd $(PWD)/docker-compose && docker compose -p ${DOCKER_PROJ_NAME} up -d)
