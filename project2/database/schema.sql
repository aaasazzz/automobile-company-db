drop database if exists CarManufacturingDB;
create database CarManufacturingDB;
use CarManufacturingDB;

drop table if exists Inventory;
drop table if exists Sales_Transactions;
drop table if exists Customer_Phones;
drop table if exists Customers;
drop table if exists Dealer_Phones;
drop table if exists Dealers;
drop table if exists Parts;
drop table if exists Vehicle_Option;
drop table if exists Options;
drop table if exists Vehicles;
drop table if exists Plants;
drop table if exists Supplier_Phones;
drop table if exists Suppliers;
drop table if exists Models;
drop table if exists Brands;

create table Brands (
    brand_id int,
    name varchar(100) not null unique,
    primary key (brand_id) );

create table Models (
    model_name varchar(100),
    year int,
    brand_id int not null,
    base_price numeric(15,2) not null,
    body_style varchar(50) not null,
    primary key (model_name, year),
    foreign key (brand_id) references Brands(brand_id),
    check (base_price >= 0),
    check (year between 1900 and 2100) );

create table Suppliers (
    supplier_id int,
    name varchar(100) not null,
    email_address varchar(100),
    primary key (supplier_id) );

create table Supplier_Phones (
    supplier_id int,
    phone_number varchar(20),
    primary key (supplier_id, phone_number),
    foreign key (supplier_id) references Suppliers(supplier_id)
        on delete cascade on update cascade );

create table Plants (
    plant_id int,
    supplier_id int not null,
    name varchar(100) not null,
    loc_street_number varchar(20),
    loc_street_name varchar(100),
    loc_city varchar(100),
    loc_state varchar(100),
    plant_type varchar(20) not null,
    primary key (plant_id),
    foreign key (supplier_id) references Suppliers(supplier_id),
    check (plant_type in ('Manufacturing', 'Assembly')) );

create table Vehicles (
    vin varchar(17),
    model_name varchar(100) not null,
    model_year int not null,
    plant_id int not null,
    mf_date date not null,
    current_status varchar(50) not null default 'In Production',
    primary key (vin),
    foreign key (model_name, model_year) references Models(model_name, year),
    foreign key (plant_id) references Plants(plant_id),
    check (current_status in ('In Production', 'In Inventory', 'Sold', 'Recalled', 'Scrapped')) );

create table Options (
    option_id int,
    option_name varchar(100) not null,
    option_type varchar(50),
    primary key (option_id) );

create table Vehicle_Option (
    vin varchar(17),
    option_id int,
    primary key (vin, option_id),
    foreign key (vin) references Vehicles(vin) on delete cascade on update cascade,
    foreign key (option_id) references Options(option_id) );

create table Parts (
    part_id int,
    production_plant_id int not null,
    assembly_plant_id int,
    parts_type varchar(100) not null,
    mf_date date not null,
    primary key (part_id),
    foreign key (production_plant_id) references Plants(plant_id),
    foreign key (assembly_plant_id) references Plants(plant_id) );

create table Dealers (
    dealer_id int,
    name varchar(100) not null,
    addr_street_number varchar(20),
    addr_street_name varchar(100),
    addr_apt_number varchar(20),
    addr_city varchar(100),
    addr_state varchar(100),
    primary key (dealer_id) );

create table Dealer_Phones (
    dealer_id int,
    phone_number varchar(20),
    primary key (dealer_id, phone_number),
    foreign key (dealer_id) references Dealers(dealer_id)
        on delete cascade on update cascade );

create table Customers (
    customer_id int,
    name varchar(100) not null,
    addr_street_number varchar(20),
    addr_street_name varchar(100),
    addr_apt_number varchar(20),
    addr_city varchar(100),
    addr_state varchar(100),
    gender char(1),
    annual_income numeric(15,2),
    primary key (customer_id),
    check (gender is null or gender in ('M', 'F', 'O')),
    check (annual_income is null or annual_income >= 0) );

create table Customer_Phones (
    customer_id int,
    phone_number varchar(20),
    primary key (customer_id, phone_number),
    foreign key (customer_id) references Customers(customer_id)
        on delete cascade on update cascade );

create table Sales_Transactions (
    transaction_id int,
    vin varchar(17) not null unique,
    dealer_id int not null,
    customer_id int not null,
    sale_date date not null,
    payment_method varchar(50) not null,
    sale_price numeric(15,2) not null,
    primary key (transaction_id),
    foreign key (vin) references Vehicles(vin),
    foreign key (dealer_id) references Dealers(dealer_id),
    foreign key (customer_id) references Customers(customer_id),
    check (sale_price >= 0),
    check (payment_method in ('Cash', 'Credit Card', 'Loan', 'Lease', 'Bank Transfer')) );

create table Inventory (
    vin varchar(17),
    dealer_id int not null,
    insert_date date not null,
    primary key (vin),
    foreign key (vin) references Vehicles(vin),
    foreign key (dealer_id) references Dealers(dealer_id) );

create index idx_models_brand on Models(brand_id);
create index idx_models_body_style on Models(body_style);
create index idx_vehicles_model on Vehicles(model_name, model_year);
create index idx_vehicles_plant on Vehicles(plant_id);
create index idx_vehicles_mf_date on Vehicles(mf_date);
create index idx_parts_type on Parts(parts_type);
create index idx_parts_mf_date on Parts(mf_date);
create index idx_parts_production on Parts(production_plant_id);
create index idx_parts_assembly on Parts(assembly_plant_id);
create index idx_sales_date on Sales_Transactions(sale_date);
create index idx_sales_dealer on Sales_Transactions(dealer_id);
create index idx_sales_customer on Sales_Transactions(customer_id);
create index idx_inventory_dealer on Inventory(dealer_id);
create index idx_inventory_date on Inventory(insert_date);

delimiter //

create trigger trg_inventory_insert
after insert on Inventory
for each row
begin
    update Vehicles set current_status = 'In Inventory' where vin = new.vin;
end//

create trigger trg_sale_insert
after insert on Sales_Transactions
for each row
begin
    update Vehicles set current_status = 'Sold' where vin = new.vin;
end//

delimiter ;
